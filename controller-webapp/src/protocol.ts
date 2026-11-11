/**
 * Troidrone Protocol — Contract C1 (proto: 2, MVP 2)
 *
 * Types, runtime validators, and serialization for communication between
 * controller-webapp and drone firmware.
 */

export type FlightState = 'DISARMED' | 'ARMED' | 'FAILSAFE' | 'LOWBATT';

export interface CtlFrame {
  t: 'ctl';
  seq: number;
  thr: number;
  str?: number;
}

export interface StopFrame {
  t: 'stop';
  seq: number;
}

export interface ArmFrame {
  t: 'arm';
  v: boolean;
}

export interface HelloFrame {
  t: 'hello';
  proto: number;
  fw: string;
}

export interface TelFrame {
  t: 'tel';
  st: FlightState;
  f: number;
  thr: number;
  str?: number;
  vb?: number;
  vpct?: number;
  age: number;
}

export interface ErrFrame {
  t: 'err';
  code: string;
  msg: string;
}

export type ClientFrame = CtlFrame | ArmFrame | StopFrame;
export type DeviceFrame = HelloFrame | TelFrame | ErrFrame;

/**
 * Clamps a number between min and max. If val is NaN, returns min.
 */
export function clamp(val: number, min: number, max: number): number {
  if (Number.isNaN(val)) {
    return min;
  }
  return Math.min(Math.max(val, min), max);
}

/**
 * Coerces a number to unsigned 32-bit integer (0..4294967295).
 */
export function toUint32(val: number): number {
  return val >>> 0;
}

/**
 * Increments a 32-bit sequence number with uint32 wrapping.
 */
export function nextSeq(seq: number): number {
  return (seq + 1) >>> 0;
}

/**
 * Checks if seq is newer than lastSeq using signed 32-bit modular arithmetic
 * (RFC 1982 / (int32_t)(seq - last_seq) > 0).
 */
export function isSeqNewer(seq: number, lastSeq: number): boolean {
  return ((seq - lastSeq) | 0) > 0;
}

export function isHelloFrame(obj: unknown): obj is HelloFrame {
  if (typeof obj !== 'object' || obj === null || Array.isArray(obj)) {
    return false;
  }
  const record = obj as Record<string, unknown>;
  return (
    record.t === 'hello' &&
    typeof record.proto === 'number' &&
    Number.isInteger(record.proto) &&
    typeof record.fw === 'string'
  );
}

export function isTelFrame(obj: unknown): obj is TelFrame {
  if (typeof obj !== 'object' || obj === null || Array.isArray(obj)) {
    return false;
  }
  const record = obj as Record<string, unknown>;
  return (
    record.t === 'tel' &&
    (record.st === 'DISARMED' ||
      record.st === 'ARMED' ||
      record.st === 'FAILSAFE' ||
      record.st === 'LOWBATT') &&
    typeof record.f === 'number' &&
    Number.isFinite(record.f) &&
    typeof record.thr === 'number' &&
    Number.isFinite(record.thr) &&
    (record.str === undefined || (typeof record.str === 'number' && Number.isFinite(record.str))) &&
    (record.vb === undefined || (typeof record.vb === 'number' && Number.isFinite(record.vb))) &&
    (record.vpct === undefined || (typeof record.vpct === 'number' && Number.isFinite(record.vpct))) &&
    typeof record.age === 'number' &&
    Number.isInteger(record.age) &&
    record.age >= 0
  );
}

export function isErrFrame(obj: unknown): obj is ErrFrame {
  if (typeof obj !== 'object' || obj === null || Array.isArray(obj)) {
    return false;
  }
  const record = obj as Record<string, unknown>;
  return (
    record.t === 'err' &&
    typeof record.code === 'string' &&
    typeof record.msg === 'string'
  );
}

export function isCtlFrame(obj: unknown): obj is CtlFrame {
  if (typeof obj !== 'object' || obj === null || Array.isArray(obj)) {
    return false;
  }
  const record = obj as Record<string, unknown>;
  return (
    record.t === 'ctl' &&
    typeof record.seq === 'number' &&
    Number.isInteger(record.seq) &&
    record.seq >= 0 &&
    record.seq <= 0xffffffff &&
    typeof record.thr === 'number' &&
    Number.isFinite(record.thr) &&
    (record.str === undefined || (typeof record.str === 'number' && Number.isFinite(record.str)))
  );
}

export function isStopFrame(obj: unknown): obj is StopFrame {
  if (typeof obj !== 'object' || obj === null || Array.isArray(obj)) {
    return false;
  }
  const record = obj as Record<string, unknown>;
  return (
    record.t === 'stop' &&
    typeof record.seq === 'number' &&
    Number.isInteger(record.seq) &&
    record.seq >= 0 &&
    record.seq <= 0xffffffff
  );
}

export function isArmFrame(obj: unknown): obj is ArmFrame {
  if (typeof obj !== 'object' || obj === null || Array.isArray(obj)) {
    return false;
  }
  const record = obj as Record<string, unknown>;
  return record.t === 'arm' && typeof record.v === 'boolean';
}

export function isClientFrame(obj: unknown): obj is ClientFrame {
  return isCtlFrame(obj) || isArmFrame(obj) || isStopFrame(obj);
}

export function isDeviceFrame(obj: unknown): obj is DeviceFrame {
  return isHelloFrame(obj) || isTelFrame(obj) || isErrFrame(obj);
}

/**
 * Safely parses and validates incoming DeviceFrame payloads (string or parsed object).
 * Extra keys are ignored. Returns null if invalid or malformed.
 */
export function parseDeviceFrame(data: unknown): DeviceFrame | null {
  let parsed: unknown = data;
  if (typeof data === 'string') {
    try {
      parsed = JSON.parse(data);
    } catch {
      return null;
    }
  }

  if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
    return null;
  }

  const record = parsed as Record<string, unknown>;

  if (record.t === 'hello') {
    if (
      typeof record.proto === 'number' &&
      Number.isInteger(record.proto) &&
      typeof record.fw === 'string'
    ) {
      return {
        t: 'hello',
        proto: record.proto,
        fw: record.fw,
      };
    }
    return null;
  }

  if (record.t === 'tel') {
    if (
      (record.st === 'DISARMED' ||
        record.st === 'ARMED' ||
        record.st === 'FAILSAFE' ||
        record.st === 'LOWBATT') &&
      typeof record.f === 'number' &&
      Number.isFinite(record.f) &&
      typeof record.thr === 'number' &&
      Number.isFinite(record.thr) &&
      typeof record.age === 'number' &&
      Number.isInteger(record.age) &&
      record.age >= 0
    ) {
      const tel: TelFrame = {
        t: 'tel',
        st: record.st,
        f: record.f,
        thr: record.thr,
        age: record.age,
      };
      if (typeof record.str === 'number' && Number.isFinite(record.str)) {
        tel.str = record.str;
      }
      if (typeof record.vb === 'number' && Number.isFinite(record.vb)) {
        tel.vb = record.vb;
      }
      if (typeof record.vpct === 'number' && Number.isFinite(record.vpct)) {
        tel.vpct = record.vpct;
      }
      return tel;
    }
    return null;
  }

  if (record.t === 'err') {
    if (typeof record.code === 'string' && typeof record.msg === 'string') {
      return {
        t: 'err',
        code: record.code,
        msg: record.msg,
      };
    }
    return null;
  }

  return null;
}

/**
 * Serializes ClientFrame payloads according to Protocol Contract C1.
 * Values are clamped and normalized before serialization.
 */
export function serializeClientFrame(frame: ClientFrame): string {
  if (frame.t === 'ctl') {
    const rawStr = frame.str ?? 0.0;
    const clean: CtlFrame = {
      t: 'ctl',
      seq: toUint32(frame.seq),
      thr: clamp(frame.thr, 0.0, 1.0),
      str: clamp(rawStr, -1.0, 1.0),
    };
    return JSON.stringify(clean);
  }

  if (frame.t === 'stop') {
    const clean: StopFrame = {
      t: 'stop',
      seq: toUint32(frame.seq),
    };
    return JSON.stringify(clean);
  }

  if (frame.t === 'arm') {
    const clean: ArmFrame = {
      t: 'arm',
      v: Boolean(frame.v),
    };
    return JSON.stringify(clean);
  }

  throw new Error(`Unsupported client frame type: ${(frame as unknown as { t?: unknown }).t}`);
}
