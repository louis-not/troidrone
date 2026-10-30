/**
 * WebSocket Link Controller.
 * Handles auto-reconnect, 25 Hz ctl send loop, hello negotiation, arming, and safety disarm hooks.
 */

import {
  type ArmFrame,
  type CtlFrame,
  type DeviceFrame,
  clamp,
  parseDeviceFrame,
  serializeClientFrame,
} from './protocol';
import { type ControllerStore, controllerStore } from './store';

export interface WebSocketLike {
  readyState: number;
  send(data: string): void;
  close(code?: number, reason?: string): void;
  onopen: ((ev: Event) => void) | null;
  onclose: ((ev: CloseEvent) => void) | null;
  onerror: ((ev: Event) => void) | null;
  onmessage: ((ev: MessageEvent) => void) | null;
}

export interface ControllerLinkOptions {
  url?: string;
  reconnect?: boolean;
  ctlIntervalMs?: number;
  store?: ControllerStore;
  webSocketFactory?: (url: string) => WebSocketLike;
  autoConnect?: boolean;
}

/**
 * Resolves default WebSocket URL according to Protocol Contract C1:
 * 1. window.location query param `?ws=...`
 * 2. `${ws/wss}://${host}/ws` in browser
 * 3. `ws://localhost:8080/ws` outside browser
 */
export function getDefaultWsUrl(): string {
  if (typeof window !== 'undefined' && window.location) {
    try {
      const wsParam = new URLSearchParams(window.location.search).get('ws');
      if (wsParam) {
        return wsParam;
      }
    } catch {
      // Ignore URLSearchParams error if search is malformed
    }
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = window.location.host;
    if (host) {
      return `${protocol}//${host}/ws`;
    }
  }
  return 'ws://localhost:8080/ws';
}

const defaultWebSocketFactory = (url: string): WebSocketLike => {
  if (typeof WebSocket === 'undefined') {
    throw new Error('WebSocket is not available in the current environment');
  }
  return new WebSocket(url);
};

export class ControllerLink {
  private readonly options: ControllerLinkOptions;
  private readonly store: ControllerStore;
  private readonly webSocketFactory: (url: string) => WebSocketLike;
  private readonly ctlIntervalMs: number;

  private ws: WebSocketLike | null = null;
  private currentUrl: string;
  private seq: number;
  private sendTimer: ReturnType<typeof setInterval> | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private currentBackoffMs: number;
  private intentionalClose = false;

  private readonly minReconnectDelayMs = 250;
  private readonly maxReconnectDelayMs = 2000;

  constructor(options: ControllerLinkOptions = {}) {
    this.options = options;
    this.store = options.store ?? controllerStore;
    this.webSocketFactory = options.webSocketFactory ?? defaultWebSocketFactory;
    this.ctlIntervalMs = options.ctlIntervalMs ?? 40;
    this.currentUrl = options.url ?? getDefaultWsUrl();
    this.seq = Date.now() >>> 0;
    this.currentBackoffMs = this.minReconnectDelayMs;

    this.attachSafetyHooks();

    if (options.autoConnect) {
      this.connect();
    }
  }

  public getStore(): ControllerStore {
    return this.store;
  }

  public getSeq(): number {
    return this.seq;
  }

  public getUrl(): string {
    return this.currentUrl;
  }

  public isConnected(): boolean {
    return (
      this.store.getState().connState === 'CONNECTED' &&
      this.ws !== null &&
      this.ws.readyState === 1
    );
  }

  public connect(overrideUrl?: string): void {
    if (overrideUrl) {
      this.currentUrl = overrideUrl;
    } else if (!this.currentUrl) {
      this.currentUrl = getDefaultWsUrl();
    }

    this.intentionalClose = false;
    this.clearReconnectTimer();
    this.stopSendLoop();

    if (this.ws) {
      const oldWs = this.ws;
      this.ws = null;
      try {
        oldWs.onopen = null;
        oldWs.onclose = null;
        oldWs.onerror = null;
        oldWs.onmessage = null;
        oldWs.close();
      } catch {
        // ignore close error
      }
    }

    this.store.setConnState('CONNECTING');

    try {
      const socket = this.webSocketFactory(this.currentUrl);
      this.ws = socket;

      socket.onopen = () => {
        if (this.ws !== socket) return;
        this.store.setConnState('CONNECTING');
      };

      socket.onmessage = (event: { data: unknown }) => {
        if (this.ws !== socket) return;
        this.handleMessage(event.data);
      };

      socket.onerror = (err) => {
        if (this.ws !== socket) return;
        console.warn('ControllerLink WebSocket error:', err);
        if (socket.readyState !== 2 && socket.readyState !== 3) {
          try {
            socket.close();
          } catch {
            // ignore
          }
        }
      };

      socket.onclose = () => {
        if (this.ws !== socket) return;
        this.handleClose();
      };
    } catch (err) {
      console.warn('Failed to initialize WebSocket:', err);
      this.handleClose();
    }
  }

  public disconnect(): void {
    this.intentionalClose = true;
    this.clearReconnectTimer();
    this.stopSendLoop();
    this.store.resetThrottleInput();

    if (this.store.getState().connState !== 'PROTO_MISMATCH') {
      this.store.setConnState('DISCONNECTED');
    }

    if (this.ws) {
      const socket = this.ws;
      this.ws = null;
      try {
        socket.onopen = null;
        socket.onclose = null;
        socket.onerror = null;
        socket.onmessage = null;
        socket.close();
      } catch {
        // ignore
      }
    }
  }

  public sendArm(v: boolean): boolean {
    if (
      this.store.getState().connState === 'CONNECTED' &&
      this.ws !== null &&
      this.ws.readyState === 1
    ) {
      this.store.resetThrottleInput();
      const frame: ArmFrame = { t: 'arm', v };
      return this.sendRaw(serializeClientFrame(frame));
    }
    return false;
  }

  public destroy(): void {
    this.disconnect();
    this.detachSafetyHooks();
  }

  private handleMessage(rawData: unknown): void {
    if (typeof rawData === 'string') {
      this.processRawString(rawData);
      return;
    }
    if (rawData instanceof ArrayBuffer) {
      const text = new TextDecoder().decode(rawData);
      this.processRawString(text);
      return;
    }
    if (typeof Blob !== 'undefined' && rawData instanceof Blob) {
      rawData
        .text()
        .then((text) => this.processRawString(text))
        .catch(() => {});
      return;
    }
    if (typeof Buffer !== 'undefined' && Buffer.isBuffer(rawData)) {
      this.processRawString(rawData.toString('utf-8'));
      return;
    }
    if (typeof rawData === 'object' && rawData !== null) {
      const frame = parseDeviceFrame(rawData);
      if (frame) {
        this.handleDeviceFrame(frame);
      }
    }
  }

  private processRawString(text: string): void {
    const frame = parseDeviceFrame(text);
    if (!frame) return;
    this.handleDeviceFrame(frame);
  }

  private handleDeviceFrame(frame: DeviceFrame): void {
    if (frame.t === 'hello') {
      if (frame.proto !== 1) {
        this.store.setHello(frame.proto, frame.fw);
        this.store.resetThrottleInput();
        this.stopSendLoop();
        return;
      }

      this.store.setHello(frame.proto, frame.fw);
      this.store.resetThrottleInput();
      this.currentBackoffMs = this.minReconnectDelayMs;
      this.sendCtl(0);
      this.startSendLoop();
      return;
    }

    if (frame.t === 'tel') {
      this.store.setTelemetry(frame);
      return;
    }
  }

  private sendCtl(thr: number): void {
    const currentSeq = this.seq;
    this.seq = (this.seq + 1) >>> 0;
    const frame: CtlFrame = {
      t: 'ctl',
      seq: currentSeq,
      thr: clamp(thr, 0.0, 1.0),
      str: 0.0,
    };
    this.sendRaw(serializeClientFrame(frame));
  }

  private sendRaw(data: string): boolean {
    if (this.ws !== null && this.ws.readyState === 1) {
      try {
        this.ws.send(data);
        return true;
      } catch {
        return false;
      }
    }
    return false;
  }

  private startSendLoop(): void {
    this.stopSendLoop();
    this.sendTimer = setInterval(() => {
      if (
        this.store.getState().connState !== 'CONNECTED' ||
        this.ws === null ||
        this.ws.readyState !== 1
      ) {
        return;
      }
      const thr = this.store.getState().throttleInput;
      this.sendCtl(thr);
    }, this.ctlIntervalMs);
  }

  private stopSendLoop(): void {
    if (this.sendTimer !== null) {
      clearInterval(this.sendTimer);
      this.sendTimer = null;
    }
  }

  private handleClose(): void {
    this.stopSendLoop();
    this.store.resetThrottleInput();

    const isProtoMismatch = this.store.getState().connState === 'PROTO_MISMATCH';
    if (!isProtoMismatch) {
      this.store.setConnState('DISCONNECTED');
    }

    this.ws = null;

    if (this.options.reconnect !== false && !this.intentionalClose && !isProtoMismatch) {
      this.scheduleReconnect();
    }
  }

  private scheduleReconnect(): void {
    this.clearReconnectTimer();
    const delay = this.currentBackoffMs;
    this.currentBackoffMs = Math.min(this.currentBackoffMs * 2, this.maxReconnectDelayMs);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.intentionalClose && this.store.getState().connState !== 'PROTO_MISMATCH') {
        this.connect();
      }
    }, delay);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  private readonly handleVisibilityChange = (): void => {
    if (typeof document !== 'undefined' && document.visibilityState === 'hidden') {
      this.triggerSafetyDisarm();
    }
  };

  private readonly handlePageHide = (): void => {
    this.triggerSafetyDisarm();
  };

  private triggerSafetyDisarm(): void {
    this.store.resetThrottleInput();
    if (
      this.store.getState().connState === 'CONNECTED' &&
      this.ws !== null &&
      this.ws.readyState === 1
    ) {
      this.sendCtl(0);
      this.sendArm(false);
    }
  }

  private attachSafetyHooks(): void {
    if (typeof document !== 'undefined' && typeof document.addEventListener === 'function') {
      document.addEventListener('visibilitychange', this.handleVisibilityChange);
    }
    if (typeof window !== 'undefined' && typeof window.addEventListener === 'function') {
      window.addEventListener('pagehide', this.handlePageHide);
    }
  }

  private detachSafetyHooks(): void {
    if (typeof document !== 'undefined' && typeof document.removeEventListener === 'function') {
      document.removeEventListener('visibilitychange', this.handleVisibilityChange);
    }
    if (typeof window !== 'undefined' && typeof window.removeEventListener === 'function') {
      window.removeEventListener('pagehide', this.handlePageHide);
    }
  }
}

export const controllerLink = new ControllerLink();
export default ControllerLink;
