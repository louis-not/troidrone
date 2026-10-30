import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { ControllerLink, getDefaultWsUrl, type WebSocketLike } from '../src/link';
import { createStore, type ControllerStore } from '../src/store';

class MockWebSocket implements WebSocketLike {
  public readyState = 0; // CONNECTING
  public sent: string[] = [];
  public closeCalls: { code?: number; reason?: string }[] = [];
  public onopen: ((ev: Event) => void) | null = null;
  public onclose: ((ev: CloseEvent) => void) | null = null;
  public onerror: ((ev: Event) => void) | null = null;
  public onmessage: ((ev: MessageEvent) => void) | null = null;

  public triggerOpen(): void {
    this.readyState = 1; // OPEN
    this.onopen?.(new Event('open'));
  }

  public triggerMessage(data: unknown): void {
    this.onmessage?.({ data } as MessageEvent);
  }

  public triggerError(err: Event = new Event('error')): void {
    this.onerror?.(err);
  }

  public triggerClose(code = 1000, reason = ''): void {
    this.readyState = 3; // CLOSED
    this.onclose?.({ code, reason } as CloseEvent);
  }

  public send(data: string): void {
    this.sent.push(data);
  }

  public close(code?: number, reason?: string): void {
    this.readyState = 3;
    this.closeCalls.push({ code, reason });
    this.onclose?.({ code: code ?? 1000, reason: reason ?? '' } as CloseEvent);
  }
}

describe('getDefaultWsUrl', () => {
  const originalWindow = globalThis.window;

  afterEach(() => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (globalThis as any).window = originalWindow;
  });

  it('returns fallback URL in non-browser environment', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (globalThis as any).window = undefined;
    expect(getDefaultWsUrl()).toBe('ws://localhost:8080/ws');
  });

  it('returns ?ws query parameter value when present', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (globalThis as any).window = {
      location: {
        search: '?ws=ws://192.168.4.1:8080/ws',
        protocol: 'http:',
        host: 'localhost:5173',
      },
    };
    expect(getDefaultWsUrl()).toBe('ws://192.168.4.1:8080/ws');
  });

  it('infers ws:// and wss:// URL from window.location when ?ws is absent', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (globalThis as any).window = {
      location: {
        search: '',
        protocol: 'http:',
        host: '10.0.0.5:3000',
      },
    };
    expect(getDefaultWsUrl()).toBe('ws://10.0.0.5:3000/ws');

    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (globalThis as any).window = {
      location: {
        search: '',
        protocol: 'https:',
        host: 'drone.example.com',
      },
    };
    expect(getDefaultWsUrl()).toBe('wss://drone.example.com/ws');
  });
});

describe('ControllerLink', () => {
  let createdSockets: MockWebSocket[];
  let store: ControllerStore;

  const mockFactory = (): MockWebSocket => {
    const socket = new MockWebSocket();
    createdSockets.push(socket);
    return socket;
  };

  beforeEach(() => {
    vi.useFakeTimers();
    createdSockets = [];
    store = createStore();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('initializes in disconnected state without autoConnect', () => {
    const link = new ControllerLink({
      store,
      webSocketFactory: mockFactory,
      autoConnect: false,
    });
    expect(createdSockets.length).toBe(0);
    expect(link.isConnected()).toBe(false);
    expect(store.getState().connState).toBe('DISCONNECTED');
    link.destroy();
  });

  it('connects and enters CONNECTING state on autoConnect', () => {
    const link = new ControllerLink({
      store,
      webSocketFactory: mockFactory,
      autoConnect: true,
      url: 'ws://127.0.0.1:8080/ws',
    });
    expect(createdSockets.length).toBe(1);
    expect(store.getState().connState).toBe('CONNECTING');
    link.destroy();
  });

  describe('Hello frame handling & 25 Hz send loop', () => {
    it('proto === 1 starts 25 Hz ctl send loop and sends initial ctl(0)', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
        ctlIntervalMs: 40,
      });

      const socket = createdSockets[0];
      socket.triggerOpen();

      // Drone sends hello with proto 1
      socket.triggerMessage(JSON.stringify({ t: 'hello', proto: 1, fw: '0.1.0' }));

      // Store transitions to CONNECTED
      expect(store.getState().connState).toBe('CONNECTED');
      expect(store.getState().fw).toBe('0.1.0');
      expect(link.isConnected()).toBe(true);

      // Immediately sends initial ctl frame with thr: 0
      expect(socket.sent.length).toBe(1);
      const initialCtl = JSON.parse(socket.sent[0]);
      expect(initialCtl.t).toBe('ctl');
      expect(initialCtl.thr).toBe(0);

      // Advance by 40 ms (1 tick)
      vi.advanceTimersByTime(40);
      expect(socket.sent.length).toBe(2);
      const secondCtl = JSON.parse(socket.sent[1]);
      expect(secondCtl.t).toBe('ctl');
      expect(secondCtl.seq).toBe((initialCtl.seq + 1) >>> 0);

      // Pilot increases throttle in store
      store.setThrottleInput(0.75);
      vi.advanceTimersByTime(40);
      expect(socket.sent.length).toBe(3);
      const thirdCtl = JSON.parse(socket.sent[2]);
      expect(thirdCtl.thr).toBe(0.75);

      link.destroy();
    });

    it('proto === 2 transitions to PROTO_MISMATCH and never sends ctl or arm', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
      });

      const socket = createdSockets[0];
      socket.triggerOpen();

      // Drone sends hello with incompatible protocol version
      socket.triggerMessage(JSON.stringify({ t: 'hello', proto: 2, fw: '0.2.0' }));

      expect(store.getState().connState).toBe('PROTO_MISMATCH');
      expect(store.getState().fw).toBe('0.2.0');
      expect(link.isConnected()).toBe(false);

      // No ctl frames should be sent
      expect(socket.sent.length).toBe(0);

      // Advance timers — still no ctl frames
      vi.advanceTimersByTime(200);
      expect(socket.sent.length).toBe(0);

      // Attempting to arm returns false and does not send anything
      const armResult = link.sendArm(true);
      expect(armResult).toBe(false);
      expect(socket.sent.length).toBe(0);

      // Closing socket in PROTO_MISMATCH should NOT trigger reconnect
      socket.triggerClose();
      vi.advanceTimersByTime(5000);
      expect(createdSockets.length).toBe(1); // No new sockets created
      expect(store.getState().connState).toBe('PROTO_MISMATCH');

      link.destroy();
    });
  });

  describe('Sequence incrementation', () => {
    it('increments sequence numbers sequentially with uint32 wrapping', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
        ctlIntervalMs: 40,
      });

      const socket = createdSockets[0];
      socket.triggerOpen();
      socket.triggerMessage(JSON.stringify({ t: 'hello', proto: 1, fw: '0.1.0' }));

      // Advance by 5 intervals (200 ms) -> 1 initial + 5 periodic = 6 frames
      vi.advanceTimersByTime(200);
      expect(socket.sent.length).toBe(6);

      const seqs = socket.sent.map((s) => JSON.parse(s).seq as number);
      for (let i = 1; i < seqs.length; i++) {
        expect(seqs[i]).toBe((seqs[i - 1] + 1) >>> 0);
      }

      link.destroy();
    });
  });

  describe('Arming frames', () => {
    it('sends arm frames and resets throttle when connected', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
      });

      const socket = createdSockets[0];
      socket.triggerOpen();
      socket.triggerMessage(JSON.stringify({ t: 'hello', proto: 1, fw: '0.1.0' }));

      // Set throttle input
      store.setThrottleInput(0.6);
      expect(store.getState().throttleInput).toBe(0.6);

      // Arm drone
      const armed = link.sendArm(true);
      expect(armed).toBe(true);
      // Safety check: throttle must be reset to 0 on arm
      expect(store.getState().throttleInput).toBe(0);

      const armMsg = JSON.parse(socket.sent[socket.sent.length - 1]);
      expect(armMsg).toEqual({ t: 'arm', v: true });

      // Disarm drone
      store.setThrottleInput(0.4);
      const disarmed = link.sendArm(false);
      expect(disarmed).toBe(true);
      expect(store.getState().throttleInput).toBe(0);

      const disarmMsg = JSON.parse(socket.sent[socket.sent.length - 1]);
      expect(disarmMsg).toEqual({ t: 'arm', v: false });

      link.destroy();
    });

    it('transmits a ctl frame with thr: 0 immediately before sending the arm { v: true } frame', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
      });

      const socket = createdSockets[0];
      socket.triggerOpen();
      socket.triggerMessage(JSON.stringify({ t: 'hello', proto: 1, fw: '0.1.0' }));

      // Drone is connected, initial ctl(0) was sent
      expect(socket.sent.length).toBe(1);

      // Pilot sets non-zero throttle input
      store.setThrottleInput(0.75);
      expect(store.getState().throttleInput).toBe(0.75);

      // Arm drone
      const armed = link.sendArm(true);
      expect(armed).toBe(true);

      // Verify two additional frames were sent: pre-arm ctl(0) followed immediately by arm(true)
      expect(socket.sent.length).toBe(3);

      const preArmCtl = JSON.parse(socket.sent[1]);
      const armFrame = JSON.parse(socket.sent[2]);

      expect(preArmCtl).toMatchObject({
        t: 'ctl',
        thr: 0,
        str: 0,
      });
      expect(preArmCtl.seq).toBeDefined();

      expect(armFrame).toEqual({
        t: 'arm',
        v: true,
      });

      // Verify store throttle input was reset to 0
      expect(store.getState().throttleInput).toBe(0);

      link.destroy();
    });

    it('rejects arming when not connected', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: false,
      });

      expect(link.sendArm(true)).toBe(false);
      expect(link.sendArm(false)).toBe(false);

      link.destroy();
    });
  });

  describe('flushZeroThrottle', () => {
    it('immediately transmits ctl frame with thr: 0 over WebSocket without waiting for the 40ms interval timer', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
        ctlIntervalMs: 40,
      });

      const socket = createdSockets[0];
      socket.triggerOpen();
      socket.triggerMessage(JSON.stringify({ t: 'hello', proto: 1, fw: '0.1.0' }));

      // Socket has sent initial ctl(0)
      expect(socket.sent.length).toBe(1);

      // Pilot increases throttle to 0.8
      store.setThrottleInput(0.8);
      expect(store.getState().throttleInput).toBe(0.8);

      // Advance only 15 ms (less than the 40 ms interval tick)
      vi.advanceTimersByTime(15);
      // No periodic ctl sent yet
      expect(socket.sent.length).toBe(1);

      // Pilot releases stick triggering flushZeroThrottle
      link.flushZeroThrottle();

      // Store throttle must be reset to 0 immediately
      expect(store.getState().throttleInput).toBe(0);

      // ctl frame with thr: 0 is transmitted immediately without waiting for the remaining 25 ms
      expect(socket.sent.length).toBe(2);
      const flushedCtl = JSON.parse(socket.sent[1]);
      expect(flushedCtl).toMatchObject({
        t: 'ctl',
        thr: 0,
        str: 0,
      });

      // When the 40ms interval timer fires, it sends another ctl frame with thr: 0
      vi.advanceTimersByTime(25);
      expect(socket.sent.length).toBe(3);
      const periodicCtl = JSON.parse(socket.sent[2]);
      expect(periodicCtl.thr).toBe(0);
      expect(periodicCtl.seq).toBe((flushedCtl.seq + 1) >>> 0);

      link.destroy();
    });

    it('resets store throttle input when disconnected without sending frames', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: false,
      });

      store.setThrottleInput(0.5);
      expect(store.getState().throttleInput).toBe(0.5);

      link.flushZeroThrottle();

      expect(store.getState().throttleInput).toBe(0);
      expect(createdSockets.length).toBe(0);

      link.destroy();
    });
  });

  describe('Telemetry handling', () => {
    it('parses incoming telemetry frames and updates store', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
      });

      const socket = createdSockets[0];
      socket.triggerOpen();
      socket.triggerMessage(JSON.stringify({ t: 'hello', proto: 1, fw: '0.1.0' }));

      socket.triggerMessage(
        JSON.stringify({ t: 'tel', st: 'ARMED', f: 2.5, thr: 0.525, age: 18 })
      );

      expect(store.getState().telemetry).toEqual({
        st: 'ARMED',
        f: 2.5,
        thr: 0.525,
        age: 18,
      });

      link.destroy();
    });

    it('handles binary message payloads (Buffer and ArrayBuffer)', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
      });

      const socket = createdSockets[0];
      socket.triggerOpen();
      socket.triggerMessage(JSON.stringify({ t: 'hello', proto: 1, fw: '0.1.0' }));

      // Buffer
      const telJson = JSON.stringify({ t: 'tel', st: 'FAILSAFE', f: 0, thr: 0, age: 501 });
      socket.triggerMessage(Buffer.from(telJson, 'utf-8'));
      expect(store.getState().telemetry?.st).toBe('FAILSAFE');

      // ArrayBuffer
      const uint8 = new TextEncoder().encode(
        JSON.stringify({ t: 'tel', st: 'DISARMED', f: 0, thr: 0, age: 9999 })
      );
      socket.triggerMessage(uint8.buffer);
      expect(store.getState().telemetry?.st).toBe('DISARMED');

      link.destroy();
    });
  });

  describe('Auto-reconnect & exponential backoff', () => {
    it('schedules reconnect with exponential backoff (250ms -> 500ms -> 1000ms -> 2000ms)', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
      });

      expect(createdSockets.length).toBe(1);

      // Attempt 1 fails
      createdSockets[0].triggerClose();
      expect(store.getState().connState).toBe('DISCONNECTED');

      // Backoff 1: 250 ms
      vi.advanceTimersByTime(249);
      expect(createdSockets.length).toBe(1);
      vi.advanceTimersByTime(1);
      expect(createdSockets.length).toBe(2);

      // Attempt 2 fails
      createdSockets[1].triggerClose();
      // Backoff 2: 500 ms
      vi.advanceTimersByTime(499);
      expect(createdSockets.length).toBe(2);
      vi.advanceTimersByTime(1);
      expect(createdSockets.length).toBe(3);

      // Attempt 3 fails
      createdSockets[2].triggerClose();
      // Backoff 3: 1000 ms
      vi.advanceTimersByTime(999);
      expect(createdSockets.length).toBe(3);
      vi.advanceTimersByTime(1);
      expect(createdSockets.length).toBe(4);

      // Attempt 4 fails
      createdSockets[3].triggerClose();
      // Backoff 4: 2000 ms (capped)
      vi.advanceTimersByTime(1999);
      expect(createdSockets.length).toBe(4);
      vi.advanceTimersByTime(1);
      expect(createdSockets.length).toBe(5);

      // Attempt 5 fails: stays at 2000 ms
      createdSockets[4].triggerClose();
      vi.advanceTimersByTime(1999);
      expect(createdSockets.length).toBe(5);
      vi.advanceTimersByTime(1);
      expect(createdSockets.length).toBe(6);

      // Success resets backoff
      createdSockets[5].triggerOpen();
      createdSockets[5].triggerMessage(JSON.stringify({ t: 'hello', proto: 1, fw: '0.1.0' }));
      createdSockets[5].triggerClose();

      // Backoff is reset to 250 ms
      vi.advanceTimersByTime(249);
      expect(createdSockets.length).toBe(6);
      vi.advanceTimersByTime(1);
      expect(createdSockets.length).toBe(7);

      link.destroy();
    });

    it('does not reconnect on intentional disconnect', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
      });

      expect(createdSockets.length).toBe(1);
      link.disconnect();

      vi.advanceTimersByTime(5000);
      expect(createdSockets.length).toBe(1);
      expect(store.getState().connState).toBe('DISCONNECTED');

      link.destroy();
    });

    it('does not reconnect when reconnect: false option is passed', () => {
      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
        reconnect: false,
      });

      createdSockets[0].triggerClose();
      vi.advanceTimersByTime(5000);
      expect(createdSockets.length).toBe(1);

      link.destroy();
    });
  });

  describe('Safety disarm hooks', () => {
    it('triggers safety disarm on visibilitychange to hidden', () => {
      // Mock document and EventTarget
      const originalDoc = globalThis.document;
      const listeners: Record<string, EventListener> = {};

      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (globalThis as any).document = {
        visibilityState: 'visible',
        addEventListener: (type: string, fn: EventListener) => {
          listeners[type] = fn;
        },
        removeEventListener: (type: string) => {
          delete listeners[type];
        },
      };

      const link = new ControllerLink({
        store,
        webSocketFactory: mockFactory,
        autoConnect: true,
      });

      const socket = createdSockets[0];
      socket.triggerOpen();
      socket.triggerMessage(JSON.stringify({ t: 'hello', proto: 1, fw: '0.1.0' }));

      store.setThrottleInput(0.9);

      // Simulate visibility change
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (globalThis as any).document.visibilityState = 'hidden';
      listeners['visibilitychange']?.(new Event('visibilitychange'));

      // Throttle is reset
      expect(store.getState().throttleInput).toBe(0);

      // Sent ctl(0) and arm(false)
      const lastTwo = socket.sent.slice(-2).map((s) => JSON.parse(s));
      expect(lastTwo[0]).toMatchObject({ t: 'ctl', thr: 0 });
      expect(lastTwo[1]).toMatchObject({ t: 'arm', v: false });

      link.destroy();
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (globalThis as any).document = originalDoc;
    });
  });
});
