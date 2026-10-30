/**
 * Controller WebApp reactive store.
 * Manages connection state, drone firmware info, telemetry, and pilot throttle input.
 */

import { clamp, type FlightState } from './protocol';

export type ConnState = 'CONNECTING' | 'CONNECTED' | 'DISCONNECTED' | 'PROTO_MISMATCH';
export type { FlightState };

export interface ControllerState {
  connState: ConnState;
  fw: string | null;
  telemetry: {
    st: FlightState;
    f: number;
    thr: number;
    age: number;
  } | null;
  throttleInput: number; // 0.0 .. 1.0
}

export interface ControllerStore {
  getState(): ControllerState;
  subscribe(listener: (state: ControllerState) => void): () => void;
  setConnState(connState: ConnState): void;
  setHello(proto: number, fw: string): void;
  setTelemetry(tel: { st: FlightState; f: number; thr: number; age: number }): void;
  setThrottleInput(val: number): void;
  resetThrottleInput(): void;
  reset(): void;
}

export const INITIAL_STATE: Readonly<ControllerState> = Object.freeze({
  connState: 'DISCONNECTED',
  fw: null,
  telemetry: null,
  throttleInput: 0,
});

export function createStore(initialState: ControllerState = INITIAL_STATE): ControllerStore {
  let state: ControllerState = { ...initialState };
  const listeners = new Set<(state: ControllerState) => void>();

  const notify = (): void => {
    const current = state;
    for (const listener of listeners) {
      try {
        listener(current);
      } catch (err) {
        console.error('Store listener error:', err);
      }
    }
  };

  return {
    getState(): ControllerState {
      return state;
    },

    subscribe(listener: (state: ControllerState) => void): () => void {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },

    setConnState(connState: ConnState): void {
      if (state.connState === connState) return;
      state = { ...state, connState };
      notify();
    },

    setHello(proto: number, fw: string): void {
      const connState: ConnState = proto === 1 ? 'CONNECTED' : 'PROTO_MISMATCH';
      state = {
        ...state,
        fw,
        connState,
        throttleInput: 0,
      };
      notify();
    },

    setTelemetry(tel: { st: FlightState; f: number; thr: number; age: number }): void {
      state = {
        ...state,
        telemetry: {
          st: tel.st,
          f: tel.f,
          thr: tel.thr,
          age: tel.age,
        },
      };
      notify();
    },

    setThrottleInput(val: number): void {
      const clamped = clamp(val, 0.0, 1.0);
      if (state.throttleInput === clamped) return;
      state = { ...state, throttleInput: clamped };
      notify();
    },

    resetThrottleInput(): void {
      if (state.throttleInput === 0) return;
      state = { ...state, throttleInput: 0 };
      notify();
    },

    reset(): void {
      state = { ...INITIAL_STATE };
      notify();
    },
  };
}

export const controllerStore = createStore();
export default controllerStore;
