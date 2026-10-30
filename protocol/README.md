# Troidrone Protocol — Contract C1 (`proto: 1`, MVP 1)

Single-source specification for MVP 1 WebSocket communication between `firmware/` and `controller-webapp/`.
Enforced JSON Schema code generation lands in MVP 3 per `docs/design/roadmap.md`; both submodules test conformance against the example payloads in `examples/`.

## Transport
- **URL:** `ws://<host>/ws` (default `ws://192.168.4.1/ws` on drone AP; UTF-8 text JSON frames `< 200` bytes).
- **Single active controller:** Newest `/ws` connection takes control; prior active socket is closed with code `4001` (`"superseded"`).

## Client → Device
| Type | Rate | Payload | Rules |
|---|---|---|---|
| `ctl` | 25 Hz | `{"t":"ctl","seq":1730208001,"thr":0.5,"str":0.0}` | `seq` is `uint32` (`(Date.now() >>> 0)` initial seed, wrapping `(int32_t)(seq - last_seq) > 0`; stale frames dropped). `thr` clamped `[0, 1]`. `str` clamped `[-1, 1]` and ignored in MVP 1. |
| `arm` | On demand | `{"t":"arm","v":true}` | `v: true` arms only from `DISARMED` when link has fresh `ctl` (`age <= 500` ms) and `thr < 0.05`. `v: false` disarms from `ARMED` or clears `FAILSAFE -> DISARMED`. |

## Device → Client
| Type | Rate | Payload | Rules |
|---|---|---|---|
| `hello` | On connect | `{"t":"hello","proto":1,"fw":"0.1.0"}` | Client blocks flight UI (`PROTO_MISMATCH`) if `proto !== 1`. |
| `tel` | 5 Hz | `{"t":"tel","st":"ARMED","f":2.5,"thr":0.525,"age":24}` | `st` in `DISARMED \| ARMED \| FAILSAFE`. `f` (Hz) and `thr` report `0.0` unless `ARMED`. `age` is ms since last accepted `ctl` (`9999` before first `ctl`). |

## Failsafe & Flap Math
- **Failsafe:** While `ARMED`, if `age > 500` ms or the active WebSocket closes/is superseded, enter `FAILSAFE` immediately (`f = 0`, `thr = 0`). Pilot must send `arm {v: false}` (`DISARMED`) before re-arming.
- **Frequency:** For `st == "ARMED"` and `thr > 0.05`, `f = 1.0 + ((thr - 0.05) / 0.95) * 3.0` (`1.0–4.0 Hz`), else `0.0 Hz`.
