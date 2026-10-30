import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { WebSocket } from 'ws';
import { createMockEspServer, MockEspServer } from '../tools/mock-esp';

class TestClient {
  public ws: WebSocket;
  private queue: unknown[] = [];
  private waiter: ((val: unknown) => void) | null = null;

  constructor(ws: WebSocket) {
    this.ws = ws;
    ws.on('message', (data) => {
      const parsed = JSON.parse(data.toString('utf8'));
      if (this.waiter) {
        const resolve = this.waiter;
        this.waiter = null;
        resolve(parsed);
      } else {
        this.queue.push(parsed);
      }
    });
  }

  public nextMessage<T = unknown>(): Promise<T> {
    if (this.queue.length > 0) {
      return Promise.resolve(this.queue.shift() as T);
    }
    return new Promise<T>((resolve) => {
      this.waiter = (val) => resolve(val as T);
    });
  }

  public send(msg: unknown): void {
    this.ws.send(JSON.stringify(msg));
  }

  public close(): void {
    if (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING) {
      try {
        this.ws.close();
      } catch {
        // ignore
      }
    }
  }
}

function connectClient(url: string): Promise<TestClient> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const client = new TestClient(ws);
    ws.once('open', () => resolve(client));
    ws.once('error', reject);
  });
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

describe('MockEspServer', () => {
  let server: MockEspServer;
  const activeClients: TestClient[] = [];

  const createClient = async (url: string): Promise<TestClient> => {
    const client = await connectClient(url);
    activeClients.push(client);
    return client;
  };

  beforeEach(() => {
    server = createMockEspServer();
  });

  afterEach(async () => {
    for (const client of activeClients) {
      client.close();
    }
    activeClients.length = 0;
    await server.stop();
  });

  describe('Lifecycle & Hello negotiation', () => {
    it('binds to an ephemeral port and reports ws url', async () => {
      const port = await server.start(0);
      expect(port).toBeGreaterThan(0);
      expect(server.getPort()).toBe(port);
      expect(server.getWsUrl()).toBe(`ws://127.0.0.1:${port}/ws`);
    });

    it('sends hello frame immediately upon client connection', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());

      const hello = await client.nextMessage<{ t: string; proto: number; fw: string }>();
      expect(hello).toEqual({
        t: 'hello',
        proto: 1,
        fw: '0.1.0-mock',
      });
    });

    it('supports custom proto version in options', async () => {
      await server.stop();
      server = createMockEspServer({ proto: 2, fw: '0.2.0-custom' });
      await server.start(0);

      const client = await createClient(server.getWsUrl());
      const hello = await client.nextMessage<{ t: string; proto: number; fw: string }>();
      expect(hello).toEqual({
        t: 'hello',
        proto: 2,
        fw: '0.2.0-custom',
      });
    });

    it('broadcasts periodic telemetry (5 Hz / 200 ms)', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());

      // Read hello first
      await client.nextMessage();

      // Next message is telemetry
      const tel = await client.nextMessage<{ t: string; st: string; f: number; thr: number; age: number }>();
      expect(tel.t).toBe('tel');
      expect(tel.st).toBe('DISARMED');
      expect(tel.f).toBe(0.0);
      expect(tel.thr).toBe(0.0);
      expect(tel.age).toBe(9999);
    });
  });

  describe('Control frame sequencing', () => {
    it('accepts initial ctl frame, enforces isSeqNewer, and drops stale/duplicate frames', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage(); // hello

      // First ctl frame accepted
      client.send({ t: 'ctl', seq: 100, thr: 0.1, str: 0.0 });
      await sleep(20);
      expect(server.getState().lastSeq).toBe(100);

      // Duplicate frame dropped
      client.send({ t: 'ctl', seq: 100, thr: 0.2, str: 0.0 });
      await sleep(20);
      expect(server.getState().lastSeq).toBe(100);

      // Stale frame dropped
      client.send({ t: 'ctl', seq: 90, thr: 0.3, str: 0.0 });
      await sleep(20);
      expect(server.getState().lastSeq).toBe(100);

      // Newer frame accepted
      client.send({ t: 'ctl', seq: 101, thr: 0.4, str: 0.0 });
      await sleep(20);
      expect(server.getState().lastSeq).toBe(101);
    });

    it('handles uint32 sequence wrapping boundaries', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage(); // hello

      // Start near max uint32
      client.send({ t: 'ctl', seq: 4294967290, thr: 0.1, str: 0.0 });
      await sleep(20);
      expect(server.getState().lastSeq).toBe(4294967290);

      // Advance to max uint32
      client.send({ t: 'ctl', seq: 4294967295, thr: 0.1, str: 0.0 });
      await sleep(20);
      expect(server.getState().lastSeq).toBe(4294967295);

      // Wrap around to 0
      client.send({ t: 'ctl', seq: 0, thr: 0.1, str: 0.0 });
      await sleep(20);
      expect(server.getState().lastSeq).toBe(0);

      // Advance to 5
      client.send({ t: 'ctl', seq: 5, thr: 0.1, str: 0.0 });
      await sleep(20);
      expect(server.getState().lastSeq).toBe(5);

      // Stale frame from before wrap (e.g. 4294967295) should be dropped
      client.send({ t: 'ctl', seq: 4294967295, thr: 0.1, str: 0.0 });
      await sleep(20);
      expect(server.getState().lastSeq).toBe(5);
    });
  });

  describe('FSM Arm guards', () => {
    it('refuses to arm if no ctl frame has been received yet', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage(); // hello

      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('DISARMED');
    });

    it('refuses to arm if raw thr >= 0.05 (throttle lockout)', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage();

      client.send({ t: 'ctl', seq: 1, thr: 0.1, str: 0.0 });
      await sleep(20);

      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('DISARMED');
    });

    it('refuses to arm if link age > 500 ms', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage();

      client.send({ t: 'ctl', seq: 1, thr: 0.0, str: 0.0 });
      await sleep(550); // wait past failsafe threshold

      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('DISARMED');
    });

    it('successfully arms when thr < 0.05 and ctl is fresh', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage();

      client.send({ t: 'ctl', seq: 1, thr: 0.02, str: 0.0 });
      await sleep(20);

      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('ARMED');
    });
  });

  describe('Flap rate & effective throttle calculation', () => {
    it('computes 0.0 flap rate when DISARMED regardless of raw throttle', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage();

      client.send({ t: 'ctl', seq: 1, thr: 0.8, str: 0.0 });
      await sleep(20);

      const st = server.getState();
      expect(st.st).toBe('DISARMED');
      expect(st.f).toBe(0.0);
      expect(st.thr).toBe(0.0);
    });

    it('computes flap rate = 1.0 + ((thr - 0.05)/0.95)*3.0 when ARMED and thr > 0.05', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage();

      // Arm with 0 throttle
      client.send({ t: 'ctl', seq: 1, thr: 0.0, str: 0.0 });
      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('ARMED');

      // Test thr = 0.05 (threshold, flap rate should be 0.0)
      client.send({ t: 'ctl', seq: 2, thr: 0.05, str: 0.0 });
      await sleep(20);
      expect(server.getState().f).toBe(0.0);
      expect(server.getState().thr).toBe(0.05);

      // Test thr = 0.525 -> 1.0 + (0.475/0.95)*3.0 = 2.5
      client.send({ t: 'ctl', seq: 3, thr: 0.525, str: 0.0 });
      await sleep(20);
      expect(server.getState().f).toBe(2.5);
      expect(server.getState().thr).toBe(0.525);

      // Test thr = 1.0 -> 1.0 + (0.95/0.95)*3.0 = 4.0
      client.send({ t: 'ctl', seq: 4, thr: 1.0, str: 0.0 });
      await sleep(20);
      expect(server.getState().f).toBe(4.0);
      expect(server.getState().thr).toBe(1.0);
    });
  });

  describe('500 ms Failsafe timeout', () => {
    it('transitions ARMED to FAILSAFE when no ctl frame is received for > 500 ms', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage();

      client.send({ t: 'ctl', seq: 1, thr: 0.0, str: 0.0 });
      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('ARMED');

      // Stop sending control frames and wait > 500 ms
      await sleep(550);

      const st = server.getState();
      expect(st.st).toBe('FAILSAFE');
      expect(st.f).toBe(0.0);
      expect(st.thr).toBe(0.0);
      expect(st.age).toBeGreaterThan(500);
    });

    it('triggers failsafe when dropControlFrames is enabled', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage();

      client.send({ t: 'ctl', seq: 1, thr: 0.0, str: 0.0 });
      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('ARMED');

      server.setDropControlFrames(true);

      // Send ctl frames continuously — should be dropped
      let seq = 2;
      const interval = setInterval(() => {
        if (client.ws.readyState === WebSocket.OPEN) {
          client.send({ t: 'ctl', seq: seq++, thr: 0.5, str: 0.0 });
        }
      }, 40);

      await sleep(550);
      clearInterval(interval);

      expect(server.getState().st).toBe('FAILSAFE');
    });
  });

  describe('Supersede rule', () => {
    it('closes prior client with code 4001 and sets FAILSAFE if was ARMED', async () => {
      await server.start(0);
      const client1 = await createClient(server.getWsUrl());
      await client1.nextMessage();

      client1.send({ t: 'ctl', seq: 1, thr: 0.0, str: 0.0 });
      client1.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('ARMED');

      const client1ClosedPromise = new Promise<{ code: number; reason: string }>((resolve) => {
        client1.ws.once('close', (code, reason) => {
          resolve({ code, reason: reason.toString('utf8') });
        });
      });

      // Client 2 connects
      const client2 = await createClient(server.getWsUrl());
      await client2.nextMessage(); // hello on client2

      const closeEvent = await client1ClosedPromise;
      expect(closeEvent.code).toBe(4001);
      expect(closeEvent.reason).toBe('superseded');

      // Server transitioned to FAILSAFE
      expect(server.getState().st).toBe('FAILSAFE');
    });
  });

  describe('Recovery from FAILSAFE', () => {
    it('ignores arm { v: true } in FAILSAFE, and recovers to DISARMED with arm { v: false }', async () => {
      await server.start(0);
      const client = await createClient(server.getWsUrl());
      await client.nextMessage();

      // Arm then let it enter failsafe
      client.send({ t: 'ctl', seq: 1, thr: 0.0, str: 0.0 });
      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('ARMED');

      await sleep(550);
      expect(server.getState().st).toBe('FAILSAFE');

      // Attempt to arm in FAILSAFE is ignored
      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('FAILSAFE');

      // Reset failsafe via arm { v: false }
      client.send({ t: 'arm', v: false });
      await sleep(20);
      expect(server.getState().st).toBe('DISARMED');

      // Now arm again from DISARMED
      client.send({ t: 'ctl', seq: 2, thr: 0.0, str: 0.0 });
      await sleep(20);
      client.send({ t: 'arm', v: true });
      await sleep(20);
      expect(server.getState().st).toBe('ARMED');
    });
  });
});
