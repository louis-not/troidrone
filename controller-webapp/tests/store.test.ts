import { describe, expect, it, vi } from 'vitest';
import { createStore, INITIAL_STATE, type ControllerState } from '../src/store';

describe('ControllerStore', () => {
  it('initializes with default state', () => {
    const store = createStore();
    expect(store.getState()).toEqual({
      connState: 'DISCONNECTED',
      fw: null,
      telemetry: null,
      throttleInput: 0,
    });
    expect(store.getState()).toEqual(INITIAL_STATE);
  });

  it('allows initializing with custom initial state', () => {
    const custom: ControllerState = {
      connState: 'CONNECTED',
      fw: '0.1.0',
      telemetry: { st: 'ARMED', f: 2.0, thr: 0.5, age: 20 },
      throttleInput: 0.5,
    };
    const store = createStore(custom);
    expect(store.getState()).toEqual(custom);
  });

  describe('setConnState', () => {
    it('updates connState and notifies subscribers', () => {
      const store = createStore();
      const listener = vi.fn();
      store.subscribe(listener);

      store.setConnState('CONNECTING');
      expect(store.getState().connState).toBe('CONNECTING');
      expect(listener).toHaveBeenCalledTimes(1);
      expect(listener).toHaveBeenCalledWith(expect.objectContaining({ connState: 'CONNECTING' }));

      store.setConnState('CONNECTED');
      expect(store.getState().connState).toBe('CONNECTED');
      expect(listener).toHaveBeenCalledTimes(2);

      store.setConnState('DISCONNECTED');
      expect(store.getState().connState).toBe('DISCONNECTED');
      expect(listener).toHaveBeenCalledTimes(3);
    });

    it('does not notify if connState is unchanged', () => {
      const store = createStore();
      const listener = vi.fn();
      store.subscribe(listener);

      store.setConnState('DISCONNECTED'); // Already DISCONNECTED
      expect(listener).not.toHaveBeenCalled();
    });
  });

  describe('setHello', () => {
    it('sets connState to CONNECTED and records fw when proto === 1', () => {
      const store = createStore();
      const listener = vi.fn();
      store.subscribe(listener);

      store.setThrottleInput(0.8);
      listener.mockClear();

      store.setHello(1, '0.1.0-esp32');
      const state = store.getState();
      expect(state.connState).toBe('CONNECTED');
      expect(state.fw).toBe('0.1.0-esp32');
      expect(state.throttleInput).toBe(0); // Safety: throttle must be reset
      expect(listener).toHaveBeenCalledTimes(1);
    });

    it('sets connState to PROTO_MISMATCH when proto !== 1', () => {
      const store = createStore();
      const listener = vi.fn();
      store.subscribe(listener);

      store.setHello(2, '0.2.0');
      const state = store.getState();
      expect(state.connState).toBe('PROTO_MISMATCH');
      expect(state.fw).toBe('0.2.0');
      expect(state.throttleInput).toBe(0);
      expect(listener).toHaveBeenCalledTimes(1);
    });

    it('handles negative or higher protocol versions as PROTO_MISMATCH', () => {
      const store = createStore();
      store.setHello(0, '0.0.0');
      expect(store.getState().connState).toBe('PROTO_MISMATCH');

      store.setHello(99, '99.0.0');
      expect(store.getState().connState).toBe('PROTO_MISMATCH');
    });
  });

  describe('setTelemetry', () => {
    it('updates telemetry data and notifies subscribers', () => {
      const store = createStore();
      const listener = vi.fn();
      store.subscribe(listener);

      const tel = { st: 'ARMED' as const, f: 3.5, thr: 0.8, age: 16 };
      store.setTelemetry(tel);

      expect(store.getState().telemetry).toEqual(tel);
      expect(listener).toHaveBeenCalledTimes(1);
    });

    it('creates an independent telemetry object copy', () => {
      const store = createStore();
      const original = { st: 'ARMED' as const, f: 1.0, thr: 0.1, age: 5 };
      store.setTelemetry(original);

      original.f = 9.9;
      expect(store.getState().telemetry?.f).toBe(1.0);
    });
  });

  describe('setThrottleInput & resetThrottleInput', () => {
    it('updates throttleInput within [0, 1] range', () => {
      const store = createStore();
      const listener = vi.fn();
      store.subscribe(listener);

      store.setThrottleInput(0.65);
      expect(store.getState().throttleInput).toBe(0.65);
      expect(listener).toHaveBeenCalledTimes(1);
    });

    it('clamps throttle values outside [0, 1]', () => {
      const store = createStore();

      store.setThrottleInput(1.5);
      expect(store.getState().throttleInput).toBe(1.0);

      store.setThrottleInput(-0.5);
      expect(store.getState().throttleInput).toBe(0.0);

      store.setThrottleInput(Number.NaN);
      expect(store.getState().throttleInput).toBe(0.0);
    });

    it('does not notify when throttleInput does not change', () => {
      const store = createStore();
      store.setThrottleInput(0.5);

      const listener = vi.fn();
      store.subscribe(listener);

      store.setThrottleInput(0.5);
      expect(listener).not.toHaveBeenCalled();
    });

    it('resetThrottleInput resets to 0.0 and notifies', () => {
      const store = createStore();
      store.setThrottleInput(0.7);

      const listener = vi.fn();
      store.subscribe(listener);

      store.resetThrottleInput();
      expect(store.getState().throttleInput).toBe(0.0);
      expect(listener).toHaveBeenCalledTimes(1);
    });

    it('resetThrottleInput does not notify if already 0', () => {
      const store = createStore();
      expect(store.getState().throttleInput).toBe(0);

      const listener = vi.fn();
      store.subscribe(listener);

      store.resetThrottleInput();
      expect(listener).not.toHaveBeenCalled();
    });
  });

  describe('reset', () => {
    it('restores state to INITIAL_STATE and notifies', () => {
      const store = createStore();
      store.setHello(1, '0.1.0');
      store.setTelemetry({ st: 'ARMED', f: 2.0, thr: 0.5, age: 10 });
      store.setThrottleInput(0.5);

      const listener = vi.fn();
      store.subscribe(listener);

      store.reset();
      expect(store.getState()).toEqual(INITIAL_STATE);
      expect(listener).toHaveBeenCalledTimes(1);
    });
  });

  describe('subscriber management', () => {
    it('supports multiple subscribers', () => {
      const store = createStore();
      const l1 = vi.fn();
      const l2 = vi.fn();

      store.subscribe(l1);
      store.subscribe(l2);

      store.setConnState('CONNECTING');
      expect(l1).toHaveBeenCalledTimes(1);
      expect(l2).toHaveBeenCalledTimes(1);
    });

    it('unsubscribing removes the listener', () => {
      const store = createStore();
      const l1 = vi.fn();
      const l2 = vi.fn();

      const unsub1 = store.subscribe(l1);
      store.subscribe(l2);

      unsub1();

      store.setConnState('CONNECTING');
      expect(l1).not.toHaveBeenCalled();
      expect(l2).toHaveBeenCalledTimes(1);
    });

    it('catches listener errors without halting other listeners', () => {
      const store = createStore();
      const badListener = vi.fn(() => {
        throw new Error('Listener failed');
      });
      const goodListener = vi.fn();

      const consoleSpy = vi.spyOn(console, 'error').mockImplementation(() => {});

      store.subscribe(badListener);
      store.subscribe(goodListener);

      store.setConnState('CONNECTING');
      expect(badListener).toHaveBeenCalledTimes(1);
      expect(goodListener).toHaveBeenCalledTimes(1);
      expect(consoleSpy).toHaveBeenCalledWith('Store listener error:', expect.any(Error));

      consoleSpy.mockRestore();
    });
  });
});
