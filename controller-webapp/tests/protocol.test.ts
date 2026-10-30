import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import {
  clamp,
  isArmFrame,
  isClientFrame,
  isCtlFrame,
  isDeviceFrame,
  isHelloFrame,
  isSeqNewer,
  isTelFrame,
  nextSeq,
  parseDeviceFrame,
  serializeClientFrame,
  toUint32,
  type ArmFrame,
  type CtlFrame,
} from '../src/protocol';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const examplesDir = resolve(__dirname, '../../protocol/examples');

function readExample(filename: string): string {
  return readFileSync(resolve(examplesDir, filename), 'utf8').trim();
}

describe('Protocol Contract C1 — Examples Validation', () => {
  it('validates arm_false.json', () => {
    const raw = readExample('arm_false.json');
    const parsed = JSON.parse(raw);

    expect(isArmFrame(parsed)).toBe(true);
    expect(isClientFrame(parsed)).toBe(true);
    expect(isDeviceFrame(parsed)).toBe(false);
    expect(parsed).toEqual({ t: 'arm', v: false });

    const serialized = serializeClientFrame(parsed);
    expect(JSON.parse(serialized)).toEqual({ t: 'arm', v: false });
  });

  it('validates arm_true.json', () => {
    const raw = readExample('arm_true.json');
    const parsed = JSON.parse(raw);

    expect(isArmFrame(parsed)).toBe(true);
    expect(isClientFrame(parsed)).toBe(true);
    expect(isDeviceFrame(parsed)).toBe(false);
    expect(parsed).toEqual({ t: 'arm', v: true });

    const serialized = serializeClientFrame(parsed);
    expect(JSON.parse(serialized)).toEqual({ t: 'arm', v: true });
  });

  it('validates ctl.json', () => {
    const raw = readExample('ctl.json');
    const parsed = JSON.parse(raw);

    expect(isCtlFrame(parsed)).toBe(true);
    expect(isClientFrame(parsed)).toBe(true);
    expect(isDeviceFrame(parsed)).toBe(false);
    expect(parsed).toEqual({ t: 'ctl', seq: 1001, thr: 0.525, str: 0.0 });

    const serialized = serializeClientFrame(parsed);
    expect(JSON.parse(serialized)).toEqual({ t: 'ctl', seq: 1001, thr: 0.525, str: 0.0 });
  });

  it('validates hello.json', () => {
    const raw = readExample('hello.json');
    const parsed = JSON.parse(raw);

    expect(isHelloFrame(parsed)).toBe(true);
    expect(isDeviceFrame(parsed)).toBe(true);
    expect(isClientFrame(parsed)).toBe(false);

    const fromParse = parseDeviceFrame(raw);
    expect(fromParse).toEqual({ t: 'hello', proto: 1, fw: '0.1.0' });
  });

  it('validates tel_armed.json', () => {
    const raw = readExample('tel_armed.json');
    const parsed = JSON.parse(raw);

    expect(isTelFrame(parsed)).toBe(true);
    expect(isDeviceFrame(parsed)).toBe(true);
    expect(isClientFrame(parsed)).toBe(false);

    const fromParse = parseDeviceFrame(raw);
    expect(fromParse).toEqual({ t: 'tel', st: 'ARMED', f: 2.5, thr: 0.525, age: 24 });
  });

  it('validates tel_disarmed.json', () => {
    const raw = readExample('tel_disarmed.json');
    const parsed = JSON.parse(raw);

    expect(isTelFrame(parsed)).toBe(true);
    expect(isDeviceFrame(parsed)).toBe(true);
    expect(isClientFrame(parsed)).toBe(false);

    const fromParse = parseDeviceFrame(raw);
    expect(fromParse).toEqual({ t: 'tel', st: 'DISARMED', f: 0.0, thr: 0.0, age: 9999 });
  });

  it('validates tel_failsafe.json', () => {
    const raw = readExample('tel_failsafe.json');
    const parsed = JSON.parse(raw);

    expect(isTelFrame(parsed)).toBe(true);
    expect(isDeviceFrame(parsed)).toBe(true);
    expect(isClientFrame(parsed)).toBe(false);

    const fromParse = parseDeviceFrame(raw);
    expect(fromParse).toEqual({ t: 'tel', st: 'FAILSAFE', f: 0.0, thr: 0.0, age: 512 });
  });
});

describe('Protocol — parseDeviceFrame', () => {
  it('returns null on malformed JSON strings', () => {
    expect(parseDeviceFrame('{ not json')).toBeNull();
    expect(parseDeviceFrame('{"t": "hello",')).toBeNull();
    expect(parseDeviceFrame('')).toBeNull();
    expect(parseDeviceFrame('undefined')).toBeNull();
  });

  it('returns null on non-object / invalid types', () => {
    expect(parseDeviceFrame(null)).toBeNull();
    expect(parseDeviceFrame(undefined)).toBeNull();
    expect(parseDeviceFrame(123)).toBeNull();
    expect(parseDeviceFrame(true)).toBeNull();
    expect(parseDeviceFrame(['hello'])).toBeNull();
  });

  it('returns null on missing or invalid fields in hello frames', () => {
    expect(parseDeviceFrame({ t: 'hello' })).toBeNull();
    expect(parseDeviceFrame({ t: 'hello', proto: 1 })).toBeNull();
    expect(parseDeviceFrame({ t: 'hello', fw: '1.0' })).toBeNull();
    expect(parseDeviceFrame({ t: 'hello', proto: '1', fw: '1.0' })).toBeNull();
    expect(parseDeviceFrame({ t: 'hello', proto: 1.5, fw: '1.0' })).toBeNull();
    expect(parseDeviceFrame({ t: 'hello', proto: 1, fw: 42 })).toBeNull();
  });

  it('returns null on missing or invalid fields in tel frames', () => {
    expect(parseDeviceFrame({ t: 'tel' })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', f: 2.5, thr: 0.5 })).toBeNull(); // missing age
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', f: 2.5, age: 10 })).toBeNull(); // missing thr
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', thr: 0.5, age: 10 })).toBeNull(); // missing f
    expect(parseDeviceFrame({ t: 'tel', f: 2.5, thr: 0.5, age: 10 })).toBeNull(); // missing st
  });

  it('rejects invalid flight states', () => {
    expect(parseDeviceFrame({ t: 'tel', st: 'UNKNOWN', f: 0, thr: 0, age: 0 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 'armed', f: 0, thr: 0, age: 0 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 'FLYING', f: 0, thr: 0, age: 0 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: '', f: 0, thr: 0, age: 0 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 123, f: 0, thr: 0, age: 0 })).toBeNull();
  });

  it('rejects non-finite numbers and negative or float ages', () => {
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', f: Number.NaN, thr: 0.5, age: 10 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', f: Number.POSITIVE_INFINITY, thr: 0.5, age: 10 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', f: 2.5, thr: Number.NaN, age: 10 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', f: 2.5, thr: Number.NEGATIVE_INFINITY, age: 10 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', f: 2.5, thr: 0.5, age: -1 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', f: 2.5, thr: 0.5, age: 10.5 })).toBeNull();
    expect(parseDeviceFrame({ t: 'tel', st: 'ARMED', f: 2.5, thr: 0.5, age: '10' })).toBeNull();
  });

  it('ignores extra unknown fields according to Contract C1', () => {
    const helloWithExtras = {
      t: 'hello',
      proto: 1,
      fw: '0.1.0',
      unknown1: 'hello',
      unknown2: 42,
    };
    expect(parseDeviceFrame(helloWithExtras)).toEqual({
      t: 'hello',
      proto: 1,
      fw: '0.1.0',
    });

    const telWithExtras = {
      t: 'tel',
      st: 'ARMED',
      f: 3.2,
      thr: 0.8,
      age: 15,
      battery: 99,
      rssi: -45,
    };
    expect(parseDeviceFrame(telWithExtras)).toEqual({
      t: 'tel',
      st: 'ARMED',
      f: 3.2,
      thr: 0.8,
      age: 15,
    });
  });

  it('rejects unknown frame types', () => {
    expect(parseDeviceFrame({ t: 'ack', id: 1 })).toBeNull();
    expect(parseDeviceFrame({ t: 'ping' })).toBeNull();
    expect(parseDeviceFrame({})).toBeNull();
  });
});

describe('Protocol — serializeClientFrame', () => {
  it('serializes ctl frames with standard values', () => {
    const frame: CtlFrame = { t: 'ctl', seq: 42, thr: 0.75, str: 0.1 };
    const serialized = serializeClientFrame(frame);
    expect(JSON.parse(serialized)).toEqual({ t: 'ctl', seq: 42, thr: 0.75, str: 0.1 });
  });

  it('clamps throttle (0..1) and steering (-1..1) for ctl frames', () => {
    const over: CtlFrame = { t: 'ctl', seq: 1, thr: 1.5, str: 2.0 };
    expect(JSON.parse(serializeClientFrame(over))).toEqual({
      t: 'ctl',
      seq: 1,
      thr: 1.0,
      str: 1.0,
    });

    const under: CtlFrame = { t: 'ctl', seq: 2, thr: -0.5, str: -3.0 };
    expect(JSON.parse(serializeClientFrame(under))).toEqual({
      t: 'ctl',
      seq: 2,
      thr: 0.0,
      str: -1.0,
    });

    const withNaN: CtlFrame = { t: 'ctl', seq: 3, thr: Number.NaN, str: Number.NaN };
    expect(JSON.parse(serializeClientFrame(withNaN))).toEqual({
      t: 'ctl',
      seq: 3,
      thr: 0.0,
      str: -1.0, // clamp(NaN, -1, 1) returns min (-1.0)
    });
  });

  it('defaults omitted str to 0.0 in ctl frame', () => {
    const noStr: CtlFrame = { t: 'ctl', seq: 5, thr: 0.3 };
    expect(JSON.parse(serializeClientFrame(noStr))).toEqual({
      t: 'ctl',
      seq: 5,
      thr: 0.3,
      str: 0.0,
    });
  });

  it('coerces seq to unsigned 32-bit integer in ctl frame', () => {
    const frame: CtlFrame = { t: 'ctl', seq: -1, thr: 0.1, str: 0.0 };
    expect(JSON.parse(serializeClientFrame(frame))).toEqual({
      t: 'ctl',
      seq: 4294967295,
      thr: 0.1,
      str: 0.0,
    });
  });

  it('serializes arm frames and coerces value to boolean', () => {
    const trueFrame: ArmFrame = { t: 'arm', v: true };
    expect(JSON.parse(serializeClientFrame(trueFrame))).toEqual({ t: 'arm', v: true });

    const falseFrame: ArmFrame = { t: 'arm', v: false };
    expect(JSON.parse(serializeClientFrame(falseFrame))).toEqual({ t: 'arm', v: false });

    const coerced = serializeClientFrame({ t: 'arm', v: 1 as unknown as boolean });
    expect(JSON.parse(coerced)).toEqual({ t: 'arm', v: true });
  });

  it('throws on unsupported client frame type', () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    expect(() => serializeClientFrame({ t: 'invalid' } as any)).toThrowError(
      /Unsupported client frame type: invalid/
    );
  });
});

describe('Protocol — isSeqNewer & uint32 wrapping', () => {
  it('correctly compares normal sequence increments', () => {
    expect(isSeqNewer(1, 0)).toBe(true);
    expect(isSeqNewer(0, 1)).toBe(false);
    expect(isSeqNewer(100, 99)).toBe(true);
    expect(isSeqNewer(99, 100)).toBe(false);
  });

  it('returns false for identical sequence numbers', () => {
    expect(isSeqNewer(10, 10)).toBe(false);
    expect(isSeqNewer(0, 0)).toBe(false);
    expect(isSeqNewer(4294967295, 4294967295)).toBe(false);
  });

  it('handles uint32 sequence wrapping boundaries', () => {
    // Wrapping from max uint32 to small values
    expect(isSeqNewer(5, 4294967290)).toBe(true);
    expect(isSeqNewer(4294967290, 5)).toBe(false);

    expect(isSeqNewer(0, 4294967295)).toBe(true);
    expect(isSeqNewer(4294967295, 0)).toBe(false);

    expect(isSeqNewer(1, 4294967295)).toBe(true);
    expect(isSeqNewer(4294967295, 1)).toBe(false);
  });

  it('handles signed 32-bit boundary window (RFC 1982 half-space)', () => {
    // Diff 0x7FFFFFFF (2147483647) is positive in signed 32-bit
    expect(isSeqNewer(2147483647, 0)).toBe(true);
    // Diff 0x80000000 (-2147483648) is negative in signed 32-bit
    expect(isSeqNewer(2147483648, 0)).toBe(false);
  });
});

describe('Protocol — Helper functions', () => {
  it('clamp behaves correctly', () => {
    expect(clamp(0.5, 0.0, 1.0)).toBe(0.5);
    expect(clamp(-1.0, 0.0, 1.0)).toBe(0.0);
    expect(clamp(2.0, 0.0, 1.0)).toBe(1.0);
    expect(clamp(Number.NaN, 0.0, 1.0)).toBe(0.0);
  });

  it('toUint32 coerces numbers to unsigned 32-bit', () => {
    expect(toUint32(0)).toBe(0);
    expect(toUint32(4294967295)).toBe(4294967295);
    expect(toUint32(-1)).toBe(4294967295);
    expect(toUint32(4294967296)).toBe(0);
    expect(toUint32(10.7)).toBe(10);
  });

  it('nextSeq increments and wraps uint32', () => {
    expect(nextSeq(0)).toBe(1);
    expect(nextSeq(100)).toBe(101);
    expect(nextSeq(4294967295)).toBe(0);
  });
});
