/**
 * Mock ESP32 WebSocket Server — Contract C1 (proto: 2, MVP 2)
 *
 * Implements ESP32 drone WebSocket protocol semantics for local development,
 * integration testing, and fault simulation.
 */

import http from 'node:http';
import type { AddressInfo } from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocket, WebSocketServer } from 'ws';
import {
  clamp,
  isArmFrame,
  isCtlFrame,
  isStopFrame,
  isSeqNewer,
  type FlightState,
  type HelloFrame,
  type TelFrame,
  type ErrFrame,
} from '../src/protocol';

export interface MockEspOptions {
  port?: number;
  proto?: number;
  fw?: string;
  initialVbat?: number;
}

export interface MockEspState {
  st: FlightState;
  f: number;
  thr: number;
  str: number;
  vb: number;
  vpct: number;
  age: number;
  lastSeq: number | null;
  activeClients: number;
  releaseLockActive: boolean;
}

function round2(val: number): number {
  return Math.round(val * 100) / 100 + 0;
}

function round3(val: number): number {
  return Math.round(val * 1000) / 1000 + 0;
}

const LIPO_TABLE_V = [3.30, 3.60, 3.70, 3.75, 3.80, 3.85, 3.90, 3.95, 4.00, 4.05, 4.10, 4.15, 4.20];
const PCT_TABLE = [0.00, 0.10, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80, 0.85, 0.90, 0.95, 1.00];

export function voltageToPercentage(packV: number, cellCount = 2): number {
  if (cellCount <= 0) return 0;
  const perCell = packV / cellCount;
  if (perCell <= LIPO_TABLE_V[0]) return 0;
  if (perCell >= LIPO_TABLE_V[LIPO_TABLE_V.length - 1]) return 100;

  for (let i = 1; i < LIPO_TABLE_V.length; ++i) {
    if (perCell <= LIPO_TABLE_V[i]) {
      const span = LIPO_TABLE_V[i] - LIPO_TABLE_V[i - 1];
      const pos = (perCell - LIPO_TABLE_V[i - 1]) / span;
      const pct = PCT_TABLE[i - 1] + pos * (PCT_TABLE[i] - PCT_TABLE[i - 1]);
      return Math.round(pct * 100);
    }
  }
  return 100;
}

export class MockEspServer {
  private readonly options: MockEspOptions;
  private port: number | null = null;
  private protoVersion: number;
  private readonly fwVersion: string;

  private state: FlightState = 'DISARMED';
  private rawThr = 0.0;
  private rawStr = 0.0;
  private lastSeq: number | null = null;
  private lastCtlMs: number | null = null;
  private releaseLockUntilMs = 0;

  // Battery simulation
  private filteredVbat: number;
  private cutoffTimerMs = 0;
  private cutoffLatched = false;

  private dropControlFrames = false;

  private httpServer: http.Server | null = null;
  private wss: WebSocketServer | null = null;
  private activeSocket: WebSocket | null = null;

  private telemetryTimer: ReturnType<typeof setInterval> | null = null;
  private tickTimer: ReturnType<typeof setInterval> | null = null;

  constructor(options: MockEspOptions = {}) {
    this.options = options;
    this.protoVersion = options.proto ?? 2;
    this.fwVersion = options.fw ?? '0.2.0-mock';
    this.filteredVbat = options.initialVbat ?? 7.82;
    if (options.port !== undefined) {
      this.port = options.port;
    }
  }

  public setVbat(volts: number): void {
    const riseAlpha = 0.12;
    const fallAlpha = 0.025;
    const alpha = volts > this.filteredVbat ? riseAlpha : fallAlpha;
    this.filteredVbat = this.filteredVbat + alpha * (volts - this.filteredVbat);
  }

  public setVbatDirect(volts: number): void {
    this.filteredVbat = volts;
  }

  public async start(port?: number): Promise<number> {
    if (this.httpServer && this.httpServer.listening && this.port !== null) {
      return this.port;
    }

    const listenPort = port !== undefined ? port : (this.options.port ?? 8080);

    const httpServer = http.createServer((req, res) => {
      const url = new URL(req.url ?? '', `http://${req.headers.host ?? '127.0.0.1'}`);

      // Battery injection endpoint: /__mock/vbat or /__sitl/vbat
      if (url.pathname === '/__mock/vbat' || url.pathname === '/__sitl/vbat') {
        const vParam = url.searchParams.get('v');
        if (vParam !== null) {
          const v = parseFloat(vParam);
          if (Number.isFinite(v)) {
            // Direct injection for test simulation
            this.setVbatDirect(v);
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ ok: true, vb: round2(this.filteredVbat), vpct: this.getBatteryPct() }));
            return;
          }
        }
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Missing or invalid v parameter' }));
        return;
      }

      // State inspector endpoint
      if (url.pathname === '/__mock/state' || url.pathname === '/__sitl/state') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(this.getState()));
        return;
      }

      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end('Not Found');
    });

    const wss = new WebSocketServer({ noServer: true });

    httpServer.on('upgrade', (req, socket, head) => {
      const url = new URL(req.url ?? '', `http://${req.headers.host ?? '127.0.0.1'}`);
      if (url.pathname === '/ws') {
        wss.handleUpgrade(req, socket, head, (ws) => {
          wss.emit('connection', ws, req);
        });
      } else {
        socket.write('HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n');
        socket.destroy();
      }
    });

    wss.on('connection', (ws) => {
      this.handleNewConnection(ws);
    });

    this.httpServer = httpServer;
    this.wss = wss;

    this.startTimers();

    return new Promise<number>((resolve, reject) => {
      const onError = (err: Error) => {
        this.clearTimers();
        reject(err);
      };

      httpServer.once('error', onError);
      httpServer.listen(listenPort, '127.0.0.1', () => {
        httpServer.removeListener('error', onError);
        const addr = httpServer.address() as AddressInfo;
        this.port = addr.port;
        resolve(this.port);
      });
    });
  }

  public async stop(): Promise<void> {
    this.clearTimers();

    if (this.activeSocket) {
      try {
        this.activeSocket.close(1000, 'Server stopping');
      } catch {
        // ignore
      }
      this.activeSocket = null;
    }

    const wss = this.wss;
    if (wss) {
      for (const client of wss.clients) {
        try {
          client.terminate();
        } catch {
          // ignore
        }
      }
      await new Promise<void>((resolve) => {
        wss.close(() => resolve());
      });
      this.wss = null;
    }

    const httpServer = this.httpServer;
    if (httpServer) {
      if (typeof httpServer.closeAllConnections === 'function') {
        httpServer.closeAllConnections();
      }
      await new Promise<void>((resolve) => {
        if (httpServer.listening) {
          httpServer.close(() => resolve());
        } else {
          resolve();
        }
      });
      this.httpServer = null;
    }

    this.state = 'DISARMED';
    this.lastSeq = null;
    this.lastCtlMs = null;
    this.rawThr = 0.0;
    this.rawStr = 0.0;
    this.releaseLockUntilMs = 0;
    this.cutoffTimerMs = 0;
    this.cutoffLatched = false;
    this.dropControlFrames = false;
  }

  public getPort(): number {
    if (this.port !== null) {
      return this.port;
    }
    return this.options.port ?? 8080;
  }

  public getWsUrl(): string {
    return `ws://127.0.0.1:${this.getPort()}/ws`;
  }

  public getState(): MockEspState {
    return {
      st: this.state,
      f: this.getFlapFrequency(),
      thr: this.getEffectiveThrottle(),
      str: this.getEffectiveSteer(),
      vb: round2(this.filteredVbat),
      vpct: this.getBatteryPct(),
      age: this.getAge(),
      lastSeq: this.lastSeq,
      activeClients: this.activeSocket && this.activeSocket.readyState === WebSocket.OPEN ? 1 : 0,
      releaseLockActive: Date.now() < this.releaseLockUntilMs,
    };
  }

  public setDropControlFrames(drop: boolean): void {
    this.dropControlFrames = drop;
  }

  public setProtoVersion(proto: number): void {
    this.protoVersion = proto;
  }

  public disconnectActiveClient(code?: number, reason?: string): void {
    if (this.activeSocket) {
      const ws = this.activeSocket;
      if (this.state === 'ARMED') {
        this.state = 'FAILSAFE';
      }
      this.activeSocket = null;
      this.lastSeq = null;
      this.lastCtlMs = null;
      this.rawThr = 0.0;
      this.rawStr = 0.0;
      try {
        ws.close(code ?? 1000, reason ?? 'Forced disconnect');
      } catch {
        // ignore
      }
    }
  }

  private handleNewConnection(ws: WebSocket): void {
    if (this.activeSocket) {
      const prior = this.activeSocket;
      this.activeSocket = null;
      if (this.state === 'ARMED') {
        this.state = 'FAILSAFE';
      }
      try {
        prior.close(4001, 'superseded');
      } catch {
        // ignore
      }
    }

    this.activeSocket = ws;
    this.lastSeq = null;
    this.lastCtlMs = null;
    this.rawThr = 0.0;
    this.rawStr = 0.0;

    const hello: HelloFrame = {
      t: 'hello',
      proto: this.protoVersion,
      fw: this.fwVersion,
    };

    try {
      ws.send(JSON.stringify(hello));
    } catch {
      // ignore
    }

    ws.on('message', (data) => {
      this.handleMessage(ws, data);
    });

    const onDisconnect = () => {
      if (this.activeSocket === ws) {
        if (this.state === 'ARMED') {
          this.state = 'FAILSAFE';
        }
        this.activeSocket = null;
        this.lastSeq = null;
        this.lastCtlMs = null;
        this.rawThr = 0.0;
        this.rawStr = 0.0;
      }
    };

    ws.on('close', onDisconnect);
    ws.on('error', onDisconnect);
  }

  private sendErr(code: string, msg: string): void {
    if (this.activeSocket && this.activeSocket.readyState === WebSocket.OPEN) {
      const errFrame: ErrFrame = {
        t: 'err',
        code,
        msg,
      };
      try {
        this.activeSocket.send(JSON.stringify(errFrame));
      } catch {
        // ignore
      }
    }
  }

  private handleMessage(ws: WebSocket, data: unknown): void {
    if (this.activeSocket !== ws) {
      return;
    }

    let parsed: unknown;
    try {
      const text = typeof data === 'string' ? data : (data as Buffer).toString('utf8');
      parsed = JSON.parse(text);
    } catch {
      return;
    }

    if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
      return;
    }

    const record = parsed as Record<string, unknown>;

    if (record.t === 'ctl') {
      if (!isCtlFrame(parsed)) {
        return;
      }
      if (this.dropControlFrames) {
        return;
      }
      if (this.lastSeq !== null && !isSeqNewer(parsed.seq, this.lastSeq)) {
        return;
      }
      this.lastSeq = parsed.seq;
      this.lastCtlMs = Date.now();

      // Check 250ms release lock
      const inReleaseLock = Date.now() < this.releaseLockUntilMs;
      if (inReleaseLock && parsed.thr > 0.05) {
        this.rawThr = 0.0;
      } else {
        this.rawThr = clamp(parsed.thr, 0.0, 1.0);
      }
      this.rawStr = parsed.str !== undefined ? clamp(parsed.str, -1.0, 1.0) : 0.0;
      return;
    }

    if (record.t === 'stop') {
      if (!isStopFrame(parsed)) {
        return;
      }
      if (this.lastSeq !== null && !isSeqNewer(parsed.seq, this.lastSeq)) {
        return;
      }
      this.lastSeq = parsed.seq;
      this.rawThr = 0.0;
      this.releaseLockUntilMs = Date.now() + 250;
      // CRUCIAL: stop frame does NOT update this.lastCtlMs
      return;
    }

    if (record.t === 'arm') {
      if (!isArmFrame(parsed)) {
        return;
      }
      if (parsed.v === true) {
        if (this.cutoffLatched) {
          this.sendErr('arm_refused', 'batt');
          return;
        }
        if (this.state !== 'DISARMED') {
          this.sendErr('arm_refused', 'state');
          return;
        }
        const age = this.getAge();
        if (this.lastCtlMs === null || age > 500) {
          this.sendErr('arm_refused', 'link');
          return;
        }
        if (this.rawThr >= 0.05) {
          this.sendErr('arm_refused', 'thr');
          return;
        }
        this.state = 'ARMED';
      } else {
        // parsed.v === false
        if (this.state === 'ARMED' || this.state === 'FAILSAFE') {
          this.state = 'DISARMED';
        }
      }
      return;
    }
  }

  private getAge(): number {
    if (this.lastCtlMs === null || this.activeSocket === null) {
      return 9999;
    }
    return Math.max(0, Math.floor(Date.now() - this.lastCtlMs));
  }

  private getBatteryPct(): number {
    return voltageToPercentage(this.filteredVbat);
  }

  private getFlapFrequency(): number {
    if (this.state === 'ARMED' && this.rawThr > 0.05) {
      return round3(1.0 + ((this.rawThr - 0.05) / 0.95) * 3.0);
    }
    return 0.0;
  }

  private getEffectiveThrottle(): number {
    if (this.state === 'ARMED') {
      return round3(this.rawThr);
    }
    return 0.0;
  }

  private getEffectiveSteer(): number {
    if (this.state === 'ARMED') {
      return round3(this.rawStr);
    }
    return 0.0;
  }

  private checkBatteryAndFailsafe(dtMs: number): void {
    // Battery cutoff check
    if (this.filteredVbat < 6.40) {
      this.cutoffTimerMs += dtMs;
      if (this.cutoffTimerMs >= 2000 && !this.cutoffLatched) {
        this.cutoffLatched = true;
        this.state = 'LOWBATT';
        this.sendErr('lowbatt', 'voltage_cutoff');
      }
    } else {
      this.cutoffTimerMs = 0;
    }

    if (this.state === 'ARMED') {
      const age = this.getAge();
      if (age > 500) {
        this.state = 'FAILSAFE';
        this.sendErr('failsafe_link', 'timeout');
      }
    }
  }

  private broadcastTelemetry(): void {
    if (this.activeSocket && this.activeSocket.readyState === WebSocket.OPEN) {
      const tel: TelFrame = {
        t: 'tel',
        st: this.state,
        f: this.getFlapFrequency(),
        thr: this.getEffectiveThrottle(),
        str: this.getEffectiveSteer(),
        vb: round2(this.filteredVbat),
        vpct: this.getBatteryPct(),
        age: this.getAge(),
      };
      try {
        this.activeSocket.send(JSON.stringify(tel));
      } catch {
        // ignore
      }
    }
  }

  private startTimers(): void {
    this.clearTimers();
    this.telemetryTimer = setInterval(() => {
      this.broadcastTelemetry();
    }, 200);

    const dtMs = 25;
    this.tickTimer = setInterval(() => {
      this.checkBatteryAndFailsafe(dtMs);
    }, dtMs);
  }

  private clearTimers(): void {
    if (this.telemetryTimer !== null) {
      clearInterval(this.telemetryTimer);
      this.telemetryTimer = null;
    }
    if (this.tickTimer !== null) {
      clearInterval(this.tickTimer);
      this.tickTimer = null;
    }
  }
}

export function createMockEspServer(options?: MockEspOptions): MockEspServer {
  return new MockEspServer(options);
}

export default MockEspServer;

const isDirectRun = Boolean(
  process.argv[1] &&
    (process.argv[1].endsWith('mock-esp.ts') ||
      process.argv[1].endsWith('mock-esp.js') ||
      (typeof import.meta !== 'undefined' &&
        import.meta.url &&
        fileURLToPath(import.meta.url) === path.resolve(process.argv[1])))
);

if (isDirectRun) {
  const envPort = process.env.PORT ?? process.env.MOCK_ESP_PORT;
  const port = envPort ? parseInt(envPort, 10) : 8080;
  const server = new MockEspServer({ port });

  server
    .start()
    .then((listeningPort) => {
      console.log(`[mock-esp] Listening on ws://127.0.0.1:${listeningPort}/ws`);
    })
    .catch((err) => {
      console.error('[mock-esp] Failed to start server:', err);
      process.exit(1);
    });

  const shutdown = async () => {
    await server.stop();
    process.exit(0);
  };

  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}
