# Troidrone Protocol — Contract C1 (`proto: 2`, MVP 2 "First Flight")

Single-source specification for MVP 2 WebSocket communication between `firmware/` and `controller-webapp/`.
Both submodules test conformance against the example payloads in `examples/`.

## Transport
- **URL:** `ws://<host>/ws` (default `ws://192.168.4.1/ws` on drone AP; UTF-8 text JSON frames `< 200` bytes).
- **Single active controller:** Newest `/ws` connection takes control; prior active socket is closed with code `4001` (`"superseded"`).

## Client → Device
| Type | Rate | Payload | Rules |
|---|---|---|---|
| `ctl` | 25 Hz | `{"t":"ctl","seq":1730208001,"thr":0.5,"str":0.0}` | `seq` is `uint32` (`(Date.now() >>> 0)` initial seed, wrapping `(int32_t)(seq - last_seq) > 0`; stale frames dropped). `thr` clamped `[0.0, 1.0]`. `str` clamped `[-1.0, 1.0]`, defaults to `0.0`. Resets 500 ms link timer. |
| `stop` | On release | `{"t":"stop","seq":1730208042}` | `seq` is `uint32`, sharing per-session sequence counter with `ctl`. Wings ease to center immediately; non-idle `ctl` (`thr > 0.05`) is ignored for **250 ms** (release lock). `stop` does **not** reset the 500 ms link timer. |
| `arm` | On demand | `{"t":"arm","v":true}` | `v: true` arms only from `DISARMED` when link has fresh `ctl` (`age <= 500` ms), `thr < 0.05`, and battery is OK (`LOWBATT` not latched). If refused, device sends `err` with `arm_refused`. `v: false` disarms from `ARMED` or clears `FAILSAFE -> DISARMED`. |

## Device → Client
| Type | Rate | Payload | Rules |
|---|---|---|---|
| `hello` | On connect | `{"t":"hello","proto":2,"fw":"0.2.0"}` | Client blocks flight UI (`PROTO_MISMATCH`) if `proto !== 2`. |
| `tel` | 5 Hz | `{"t":"tel","st":"ARMED","f":2.5,"thr":0.525,"str":-0.3,"vb":7.82,"vpct":71,"age":24}` | `st` in `DISARMED \| ARMED \| FAILSAFE \| LOWBATT`. `f` (Hz), `thr`, and `str` report `0.0` unless `ARMED`. `vb` is pack voltage (filtered EMA, 2 decimals). `vpct` is integer battery % (0..100). `age` is ms since last accepted `ctl` (`9999` before first `ctl`). |
| `err` | On error / event | `{"t":"err","code":"arm_refused","msg":"batt"}` | `code` in `arm_refused`, `failsafe_link`, `failsafe_close`, `lowbatt`. `msg` provides context (`batt`, `thr`, `link`, `timeout`, etc.). |

## Safety FSM & Control Math
- **Flight States:**
  - `DISARMED`: Servos held at neutral (90°), PWM unpowered or idle.
  - `ARMED`: Flapping active per throttle and steering inputs. Screen Wake Lock active on client.
  - `FAILSAFE`: Entered when `age > 500` ms or active WebSocket closes/superseded. Wings ramp to center. Pilot must send `arm {v: false}` before re-arming.
  - `LOWBATT`: Entered from any state when filtered `vb < 6.4 V` sustained for `> 2.0 s`. Wings ease to center, PWM detaches, low-battery LED solid ON. **Latched until reboot**; arming refused.
- **Flap Frequency:**
  - For `st == "ARMED"` and `thr > 0.05`: $f = 1.0 + \frac{thr - 0.05}{0.95} \times 3.0\text{ Hz}$ ($1.0\text{--}4.0\text{ Hz}$), else $0.0\text{ Hz}$.
- **Differential Steering Mixer:**
  - Authority: $\text{authority} = \min(70^\circ, 10^\circ + thr^2 \times 50^\circ)$.
  - Normalized steering input: $s = \frac{str \times \text{authority}}{70^\circ}$.
  - Base stroke with turn compensation: $\text{base} = \text{travel} \times (1 + |s| \times 0.12)$.
  - Differential deflection: $\Delta = \operatorname{clamp}(s \times 1.15 \times \text{base} \times 0.25, \pm 0.25 \times \text{base})$.
  - Per-wing stroke: $\text{left} = \text{base} + \Delta$, $\text{right} = \text{base} - \Delta$.
- **Idle Lean:**
  - When `thr <= 0.05`, non-zero `str` common-mode tilts both wings up to $\pm 15^\circ$ via a $45\text{ ms}$ low-pass filter ($\text{left} = 90^\circ - \text{lean}$, $\text{right} = 90^\circ - \text{lean}$).
