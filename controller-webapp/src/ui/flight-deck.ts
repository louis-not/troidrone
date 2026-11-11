/**
 * TROIDRONE MK-2 Physical RC Flight Transmitter UI Component (MVP 2).
 * Mode 2 Dual Gimbals (Throttle vertical with dead-man snap, Steer horizontal with spring return),
 * mechanical shoulder switches, live 4-channel servo monitor, center LCD touchscreen.
 */

import type { ControllerState, FlightState } from '../store';

export interface FlightDeckCallbacks {
  onThrottleChange: (value: number) => void;
  onSteerChange: (value: number) => void;
  onStickRelease: () => void;
  onThrottleReset: () => void;
  onArmAction: (arm: boolean) => void;
}

export interface FlightDeckUI {
  element: HTMLElement;
  update: (state: ControllerState) => void;
  destroy: () => void;
}

export function createFlightDeckUI(callbacks: FlightDeckCallbacks): FlightDeckUI {
  const container = document.createElement('div');
  container.id = 'rc-transmitter-chassis';

  container.innerHTML = `
    <!-- Side Textured Grips -->
    <div class="rc-side-grip rc-grip-left"></div>
    <div class="rc-side-grip rc-grip-right"></div>

    <!-- 1. Top Shoulder -->
    <header class="rc-top-shoulder">
      <div class="rc-switches-group">
        <!-- Switch SA: Arm / Disarm Interlock -->
        <div class="rc-toggle-unit" id="switch-sa" data-testid="switch-sa" title="Switch SA: Arm / Disarm Interlock">
          <span class="rc-switch-label">SW [SA]</span>
          <div class="rc-lever-housing" id="sa-housing">
            <div class="rc-lever-stem"></div>
          </div>
        </div>

        <!-- Switch SB: Flight Mode -->
        <div class="rc-toggle-unit" id="switch-sb" title="Switch SB: Mode">
          <span class="rc-switch-label">SW [SB]</span>
          <div class="rc-lever-housing" id="sb-housing">
            <div class="rc-lever-stem"></div>
          </div>
        </div>
      </div>

      <!-- Center Brand Crest -->
      <div class="rc-brand-crest">
        <div class="rc-brand-logo">TROIDRONE <span>MK-2</span></div>
        <div class="rc-model-tag">S3 2.4GHz</div>
      </div>

      <div class="rc-switches-group">
        <!-- Switch SC: Reserved -->
        <div class="rc-toggle-unit" id="switch-sc" title="Switch SC">
          <span class="rc-switch-label">SW [SC]</span>
          <div class="rc-lever-housing" id="sc-housing">
            <div class="rc-lever-stem"></div>
          </div>
        </div>

        <!-- Switch SD: Emergency Motor Cut -->
        <div class="rc-toggle-unit" id="switch-sd" data-testid="switch-sd" title="Switch SD: Emergency Motor Cut">
          <span class="rc-switch-label">SW [SD]</span>
          <div class="rc-lever-housing active" id="sd-housing">
            <div class="rc-lever-stem"></div>
          </div>
        </div>
      </div>
    </header>

    <!-- 2. Main Cockpit -->
    <main class="rc-main-cockpit">
      <!-- LEFT GIMBAL (Mode 2: Throttle Vertical 0..100%) -->
      <section class="rc-gimbal-column" aria-label="Left Gimbal Throttle Control">
        <div
          class="rc-gimbal-bezel"
          id="left-gimbal-bezel"
          data-testid="throttle-stick"
          role="slider"
          aria-label="Throttle"
          aria-valuemin="0"
          aria-valuemax="100"
          aria-valuenow="0"
          tabindex="0"
          title="Left Gimbal: Drag Up for Throttle (Release: Dead-Man Snap to 0)"
        >
          <div class="rc-collar-circle"></div>
          <div class="rc-axis-tick-v"></div>
          <div class="rc-axis-tick-h"></div>
          <div class="rc-center-red-v"></div>
          <div class="rc-center-red-h"></div>

          <span class="rc-tick-label tick-top">100% THR</span>
          <span class="rc-tick-label tick-bot">0% THR</span>
          <span class="rc-tick-label tick-left">-10</span>
          <span class="rc-tick-label tick-right">+10</span>

          <!-- Left Stick Puck Head -->
          <div class="rc-stick-puck" id="left-stick-puck" data-testid="throttle-thumb">
            <div class="rc-knurled-crown"></div>
          </div>
        </div>

        <div class="rc-trim-row">
          <span>THROTTLE: <strong id="readout-thr-pct" data-testid="throttle-value" style="color: #fff;">0%</strong></span>
          <span class="rc-trim-rocker">T3: 0</span>
        </div>
      </section>

      <!-- CENTER: Integrated Color LCD Screen -->
      <section class="rc-center-lcd" aria-label="Transmitter LCD Screen">
        <!-- Top Status Bar -->
        <div class="rc-lcd-topbar">
          <div>
            <span>MDL: <strong>TROIDRONE-1</strong></span>
          </div>
          <div>
            <span>LINK: <strong id="lcd-link-age" data-testid="link-age">--- ms</strong></span>
          </div>
          <div>
            <span>2S BATT: <strong class="lcd-tx-batt nominal" id="lcd-batt-volts" data-testid="batt-volts">--- V</strong> (<span id="lcd-batt-pct" data-testid="batt-pct">--%</span>)</span>
          </div>
        </div>

        <div class="rc-lcd-dashboard">
          <!-- Timers & Master State Capsule -->
          <div class="rc-lcd-master-row">
            <div class="rc-flight-timer" id="lcd-timer" data-testid="flight-timer">00:00.0</div>
            <div
              class="rc-state-capsule disarmed"
              id="lcd-fsm-capsule"
              data-testid="state-badge"
              role="status"
              aria-live="polite"
            >DISCONNECTED</div>
          </div>

          <!-- Release Lock Banner -->
          <div class="rc-lcd-lock-banner" id="lcd-lock-banner" data-testid="lock-banner">
            ⚠ 250ms RELEASE LOCK ACTIVE (THROTTLE LOCKED)
          </div>

          <!-- Flap Frequency & Steer Readout -->
          <div class="rc-lcd-metrics-grid">
            <div class="rc-metric-tile">
              <div class="rc-metric-tile-label">Flap Frequency</div>
              <div class="rc-metric-tile-val"><span id="lcd-flap-hz" data-testid="flap-rate">0.0 Hz</span></div>
            </div>
            <div class="rc-metric-tile">
              <div class="rc-metric-tile-label">Steer Deflection</div>
              <div class="rc-metric-tile-val" id="lcd-steer-bias">0.00</div>
            </div>
          </div>

          <!-- Live 4-Channel Servo Monitor -->
          <div class="rc-channel-monitor">
            <div class="rc-ch-row">
              <span style="width: 46px;">CH1 THR</span>
              <div class="rc-ch-bar-track">
                <div class="rc-ch-bar-fill" id="ch1-bar" data-testid="ch1-bar" style="width: 0%; left: 0;"></div>
              </div>
              <span style="width: 32px; text-align: right;" id="ch1-val">0%</span>
            </div>
            <div class="rc-ch-row">
              <span style="width: 46px;">CH2 STR</span>
              <div class="rc-ch-bar-track">
                <div class="rc-ch-bar-fill" id="ch2-bar" data-testid="ch2-bar" style="width: 0%; left: 50%;"></div>
              </div>
              <span style="width: 32px; text-align: right;" id="ch2-val">0.00</span>
            </div>
            <div class="rc-ch-row">
              <span style="width: 46px;">CH3 W-L</span>
              <div class="rc-ch-bar-track">
                <div class="rc-ch-bar-fill" id="ch3-bar" data-testid="ch3-bar" style="width: 50%; left: 0;"></div>
              </div>
              <span style="width: 32px; text-align: right;" id="ch3-val">90°</span>
            </div>
            <div class="rc-ch-row">
              <span style="width: 46px;">CH4 W-R</span>
              <div class="rc-ch-bar-track">
                <div class="rc-ch-bar-fill" id="ch4-bar" data-testid="ch4-bar" style="width: 50%; left: 0;"></div>
              </div>
              <span style="width: 32px; text-align: right;" id="ch4-val">90°</span>
            </div>
          </div>

          <!-- Bottom Safety Triggers -->
          <div class="rc-safety-trigger-strip">
            <button class="rc-hold-arm-btn" id="btn-hold-arm" data-testid="arm-button" type="button" disabled><div class="rc-arm-fill-indicator" id="arm-progress-fill"></div><span id="arm-label-text">DISCONNECTED</span></button>
            <button class="rc-cutoff-btn" id="btn-emergency-cutoff" data-testid="emergency-cutoff" type="button">
              <span>⛔</span>
              <span>EMERGENCY MOTOR CUT</span>
            </button>
          </div>
        </div>
      </section>

      <!-- RIGHT GIMBAL (Mode 2: Steering Horizontal -100%..+100%) -->
      <section class="rc-gimbal-column" aria-label="Right Gimbal Steering Control">
        <div
          class="rc-gimbal-bezel"
          id="right-gimbal-bezel"
          data-testid="steer-stick"
          role="slider"
          aria-label="Steering"
          aria-valuemin="-100"
          aria-valuemax="100"
          aria-valuenow="0"
          tabindex="0"
          title="Right Gimbal: Drag Left/Right for Steering (Spring Return to Center)"
        >
          <div class="rc-collar-circle"></div>
          <div class="rc-axis-tick-v"></div>
          <div class="rc-axis-tick-h"></div>
          <div class="rc-center-red-v"></div>
          <div class="rc-center-red-h"></div>

          <span class="rc-tick-label tick-top">+10</span>
          <span class="rc-tick-label tick-bot">-10</span>
          <span class="rc-tick-label tick-left">◀ L</span>
          <span class="rc-tick-label tick-right">R ▶</span>

          <!-- Right Stick Puck Head -->
          <div class="rc-stick-puck" id="right-stick-puck" data-testid="steer-thumb">
            <div class="rc-knurled-crown"></div>
          </div>
        </div>

        <div class="rc-trim-row">
          <span>STEER: <strong id="readout-str-val" data-testid="steer-value" style="color: #fff;">0.00</strong></span>
          <span class="rc-trim-rocker">T1: 0</span>
        </div>
      </section>
    </main>
  `;

  // Element handles
  const leftBezel = container.querySelector<HTMLElement>('#left-gimbal-bezel')!;
  const leftPuck = container.querySelector<HTMLElement>('#left-stick-puck')!;
  const rightBezel = container.querySelector<HTMLElement>('#right-gimbal-bezel')!;
  const rightPuck = container.querySelector<HTMLElement>('#right-stick-puck')!;

  const thrReadout = container.querySelector<HTMLElement>('#readout-thr-pct')!;
  const strReadout = container.querySelector<HTMLElement>('#readout-str-val')!;

  const stateBadge = container.querySelector<HTMLElement>('#lcd-fsm-capsule')!;
  const linkAgeEl = container.querySelector<HTMLElement>('#lcd-link-age')!;
  const flapHzEl = container.querySelector<HTMLElement>('#lcd-flap-hz')!;
  const steerBiasEl = container.querySelector<HTMLElement>('#lcd-steer-bias')!;
  const timerEl = container.querySelector<HTMLElement>('#lcd-timer')!;
  const battVoltsEl = container.querySelector<HTMLElement>('#lcd-batt-volts')!;
  const battPctEl = container.querySelector<HTMLElement>('#lcd-batt-pct')!;
  const lockBannerEl = container.querySelector<HTMLElement>('#lcd-lock-banner')!;

  const ch1Bar = container.querySelector<HTMLElement>('#ch1-bar')!;
  const ch1Val = container.querySelector<HTMLElement>('#ch1-val')!;
  const ch2Bar = container.querySelector<HTMLElement>('#ch2-bar')!;
  const ch2Val = container.querySelector<HTMLElement>('#ch2-val')!;
  const ch3Bar = container.querySelector<HTMLElement>('#ch3-bar')!;
  const ch3Val = container.querySelector<HTMLElement>('#ch3-val')!;
  const ch4Bar = container.querySelector<HTMLElement>('#ch4-bar')!;
  const ch4Val = container.querySelector<HTMLElement>('#ch4-val')!;

  const armButton = container.querySelector<HTMLButtonElement>('#btn-hold-arm')!;
  const armLabel = container.querySelector<HTMLElement>('#arm-label-text')!;
  const armProgressFill = container.querySelector<HTMLElement>('#arm-progress-fill')!;
  const cutoffButton = container.querySelector<HTMLButtonElement>('#btn-emergency-cutoff')!;

  const switchSa = container.querySelector<HTMLElement>('#switch-sa')!;
  const saHousing = container.querySelector<HTMLElement>('#sa-housing')!;
  const switchSd = container.querySelector<HTMLElement>('#switch-sd')!;

  // Internal state
  let currentFlightState: FlightState | 'DISCONNECTED' | 'PROTO_MISMATCH' = 'DISCONNECTED';
  let isConnected = false;
  let armedStartTime: number | null = null;
  let timerInterval: ReturnType<typeof setInterval> | null = null;
  let releaseLockTimeout: ReturnType<typeof setTimeout> | null = null;
  let wakeLockSentinel: unknown = null;

  // Gimbal control constants
  const GIMBAL_TRAVEL_RADIUS = 50; // px travel from center

  // --- Left Gimbal (Throttle) Handling ---
  let isDraggingLeft = false;
  let leftPointerId: number | null = null;

  const updateThrottleFromPointer = (clientY: number) => {
    const rect = leftBezel.getBoundingClientRect();
    const centerY = rect.top + rect.height / 2;
    // Top of stroke (+radius) = 1.0, bottom of stroke (-radius) = 0.0
    const rawDeltaY = centerY - clientY; // positive when dragged UP
    const normalized = (rawDeltaY + GIMBAL_TRAVEL_RADIUS) / (2 * GIMBAL_TRAVEL_RADIUS);
    const clamped = Math.max(0, Math.min(1, normalized));

    // Update thumb translation
    const currentTranslateY = GIMBAL_TRAVEL_RADIUS - clamped * (2 * GIMBAL_TRAVEL_RADIUS);
    leftPuck.style.transform = `translate(0px, ${currentTranslateY}px)`;

    callbacks.onThrottleChange(clamped);
  };

  const snapLeftToZero = () => {
    if (leftPointerId !== null && leftBezel.hasPointerCapture(leftPointerId)) {
      try { leftBezel.releasePointerCapture(leftPointerId); } catch { /* ignore */ }
    }
    isDraggingLeft = false;
    leftPointerId = null;
    leftBezel.classList.remove('gimbal--active');

    // Dead-man snap: puck moves instantly to bottom
    leftPuck.style.transform = `translate(0px, ${GIMBAL_TRAVEL_RADIUS}px)`;

    // Fire callbacks
    callbacks.onThrottleReset();
    callbacks.onStickRelease();

    // Show release lock banner for 250 ms
    showReleaseLock(250);
  };

  leftBezel.addEventListener('pointerdown', (e) => {
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    leftPointerId = e.pointerId;
    isDraggingLeft = true;
    leftBezel.classList.add('gimbal--active');
    try { leftBezel.setPointerCapture(e.pointerId); } catch { /* ignore */ }
    updateThrottleFromPointer(e.clientY);
  });

  leftBezel.addEventListener('pointermove', (e) => {
    if (isDraggingLeft && (leftPointerId === null || leftPointerId === e.pointerId)) {
      updateThrottleFromPointer(e.clientY);
    }
  });

  leftBezel.addEventListener('pointerup', () => snapLeftToZero());
  leftBezel.addEventListener('pointercancel', () => snapLeftToZero());

  // --- Right Gimbal (Steering) Handling ---
  let isDraggingRight = false;
  let rightPointerId: number | null = null;

  const updateSteerFromPointer = (clientX: number) => {
    const rect = rightBezel.getBoundingClientRect();
    const centerX = rect.left + rect.width / 2;
    const deltaX = clientX - centerX; // positive when dragged RIGHT
    const clampedPx = Math.max(-GIMBAL_TRAVEL_RADIUS, Math.min(GIMBAL_TRAVEL_RADIUS, deltaX));
    const normalized = clampedPx / GIMBAL_TRAVEL_RADIUS; // -1.0 .. +1.0

    rightPuck.style.transform = `translate(${clampedPx}px, 0px)`;
    callbacks.onSteerChange(normalized);
  };

  const springRightToCenter = () => {
    if (rightPointerId !== null && rightBezel.hasPointerCapture(rightPointerId)) {
      try { rightBezel.releasePointerCapture(rightPointerId); } catch { /* ignore */ }
    }
    isDraggingRight = false;
    rightPointerId = null;
    rightBezel.classList.remove('gimbal--active');

    // Spring return to center
    rightPuck.style.transform = `translate(0px, 0px)`;
    callbacks.onSteerChange(0.0);
  };

  rightBezel.addEventListener('pointerdown', (e) => {
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    rightPointerId = e.pointerId;
    isDraggingRight = true;
    rightBezel.classList.add('gimbal--active');
    try { rightBezel.setPointerCapture(e.pointerId); } catch { /* ignore */ }
    updateSteerFromPointer(e.clientX);
  });

  rightBezel.addEventListener('pointermove', (e) => {
    if (isDraggingRight && (rightPointerId === null || rightPointerId === e.pointerId)) {
      updateSteerFromPointer(e.clientX);
    }
  });

  rightBezel.addEventListener('pointerup', () => springRightToCenter());
  rightBezel.addEventListener('pointercancel', () => springRightToCenter());

  // --- Release Lock Banner Helper ---
  const showReleaseLock = (durationMs = 250) => {
    lockBannerEl.style.display = 'block';
    if (releaseLockTimeout !== null) {
      clearTimeout(releaseLockTimeout);
    }
    releaseLockTimeout = setTimeout(() => {
      lockBannerEl.style.display = 'none';
      releaseLockTimeout = null;
    }, durationMs);
  };

  // --- Flight Timer ---
  const startFlightTimer = () => {
    if (timerInterval !== null) return;
    armedStartTime = Date.now();
    timerInterval = setInterval(() => {
      if (armedStartTime === null) return;
      const elapsed = Date.now() - armedStartTime;
      const totalSec = elapsed / 1000;
      const mins = Math.floor(totalSec / 60);
      const secs = (totalSec % 60).toFixed(1);
      timerEl.textContent = `${String(mins).padStart(2, '0')}:${String(secs).padStart(4, '0')}`;
    }, 100);
  };

  const stopFlightTimer = () => {
    if (timerInterval !== null) {
      clearInterval(timerInterval);
      timerInterval = null;
    }
    armedStartTime = null;
  };

  // --- Hold-To-Arm Implementation ---
  let armHoldTimer: ReturnType<typeof setInterval> | null = null;
  let holdProgress = 0;

  const cancelArmHold = () => {
    if (armHoldTimer !== null) {
      clearInterval(armHoldTimer);
      armHoldTimer = null;
    }
    holdProgress = 0;
    armProgressFill.style.width = '0%';
  };

  armButton.addEventListener('pointerdown', () => {
    if (armButton.disabled || !isConnected) return;
    if (currentFlightState === 'ARMED') {
      callbacks.onArmAction(false);
      return;
    }
    if (currentFlightState === 'FAILSAFE') {
      callbacks.onArmAction(false);
      return;
    }
    if (currentFlightState === 'DISARMED') {
      cancelArmHold();
      holdProgress = 0;
      armHoldTimer = setInterval(() => {
        holdProgress += 50;
        const pct = Math.min(100, (holdProgress / 1000) * 100);
        armProgressFill.style.width = `${pct}%`;
        if (holdProgress >= 1000) {
          cancelArmHold();
          callbacks.onArmAction(true);
        }
      }, 50);
    }
  });

  armButton.addEventListener('pointerup', cancelArmHold);
  armButton.addEventListener('pointercancel', cancelArmHold);
  armButton.addEventListener('pointerleave', cancelArmHold);

  // Standard click fallback for instant tests
  armButton.addEventListener('click', () => {
    if (armButton.disabled || !isConnected) return;
    if (currentFlightState === 'ARMED') {
      callbacks.onArmAction(false);
    } else if (currentFlightState === 'FAILSAFE') {
      callbacks.onArmAction(false);
    } else if (currentFlightState === 'DISARMED') {
      callbacks.onArmAction(true);
    }
  });

  // Emergency Motor Cut button
  cutoffButton.addEventListener('click', () => {
    callbacks.onArmAction(false);
    callbacks.onThrottleReset();
    callbacks.onStickRelease();
  });

  // Switch SA Click (Toggle Arm/Disarm)
  switchSa.addEventListener('click', () => {
    if (!isConnected) return;
    if (currentFlightState === 'ARMED') {
      callbacks.onArmAction(false);
    } else if (currentFlightState === 'DISARMED') {
      callbacks.onArmAction(true);
    } else if (currentFlightState === 'FAILSAFE') {
      callbacks.onArmAction(false);
    }
  });

  // Switch SD Click (Emergency Cut)
  switchSd.addEventListener('click', () => {
    callbacks.onArmAction(false);
    callbacks.onThrottleReset();
    callbacks.onStickRelease();
  });

  // --- Screen Wake Lock Management ---
  const requestWakeLock = async () => {
    if ('wakeLock' in navigator) {
      try {
        wakeLockSentinel = await (navigator as unknown as { wakeLock: { request: (type: string) => Promise<unknown> } }).wakeLock.request('screen');
      } catch {
        // ignore
      }
    }
  };

  const releaseWakeLock = () => {
    if (wakeLockSentinel) {
      try {
        (wakeLockSentinel as { release: () => Promise<void> }).release();
      } catch {
        // ignore
      }
      wakeLockSentinel = null;
    }
  };

  return {
    element: container,

    update(state: ControllerState): void {
      const conn = state.connState;
      isConnected = conn === 'CONNECTED';
      const tel = state.telemetry;

      // 1. Connection / Master State Banner
      stateBadge.className = 'rc-state-capsule';
      if (conn === 'DISCONNECTED') {
        currentFlightState = 'DISCONNECTED';
        stateBadge.classList.add('disarmed');
        stateBadge.textContent = 'DISCONNECTED';
        armButton.disabled = true;
        armLabel.textContent = 'DISCONNECTED';
        cutoffButton.style.display = 'none';
        saHousing.classList.remove('active');
        stopFlightTimer();
        releaseWakeLock();
      } else if (conn === 'CONNECTING') {
        currentFlightState = 'DISCONNECTED';
        stateBadge.classList.add('disarmed');
        stateBadge.textContent = 'CONNECTING';
        armButton.disabled = true;
        armLabel.textContent = 'CONNECTING...';
        cutoffButton.style.display = 'none';
        saHousing.classList.remove('active');
        stopFlightTimer();
        releaseWakeLock();
      } else if (conn === 'PROTO_MISMATCH') {
        currentFlightState = 'PROTO_MISMATCH';
        stateBadge.classList.add('proto-mismatch');
        stateBadge.textContent = 'PROTO MISMATCH';
        armButton.disabled = true;
        armLabel.textContent = 'PROTO MISMATCH';
        cutoffButton.style.display = 'none';
        saHousing.classList.remove('active');
        stopFlightTimer();
        releaseWakeLock();
      } else {
        // CONNECTED: read telemetry state
        const st: FlightState = tel?.st ?? 'DISARMED';
        currentFlightState = st;

        if (st === 'ARMED') {
          stateBadge.classList.add('armed');
          stateBadge.textContent = 'ARMED';
          armButton.disabled = false;
          armLabel.textContent = 'DISARM DRONE';
          cutoffButton.style.display = 'flex';
          saHousing.classList.add('active');
          startFlightTimer();
          requestWakeLock();
        } else if (st === 'FAILSAFE') {
          stateBadge.classList.add('failsafe');
          stateBadge.textContent = 'FAILSAFE';
          armButton.disabled = false;
          armLabel.textContent = 'RESET FAILSAFE';
          cutoffButton.style.display = 'none';
          saHousing.classList.remove('active');
          stopFlightTimer();
          releaseWakeLock();
        } else if (st === 'LOWBATT') {
          stateBadge.classList.add('lowbatt');
          stateBadge.textContent = 'LOWBATT';
          armButton.disabled = true;
          armLabel.textContent = 'LOW BATTERY';
          cutoffButton.style.display = 'none';
          saHousing.classList.remove('active');
          stopFlightTimer();
          releaseWakeLock();
        } else {
          // DISARMED
          stateBadge.classList.add('disarmed');
          stateBadge.textContent = 'DISARMED';
          armButton.disabled = false;
          armLabel.textContent = 'ARM DRONE';
          cutoffButton.style.display = 'none';
          saHousing.classList.remove('active');
          stopFlightTimer();
          releaseWakeLock();
        }
      }

      // 2. Telemetry metrics
      if (tel) {
        linkAgeEl.textContent = `${tel.age} ms`;
        if (tel.age > 200) {
          linkAgeEl.setAttribute('data-warning', 'true');
        } else {
          linkAgeEl.removeAttribute('data-warning');
        }

        flapHzEl.textContent = `${tel.f.toFixed(1)} Hz`;
        steerBiasEl.textContent = (tel.str ?? state.steerInput).toFixed(2);

        if (tel.vb !== undefined) {
          battVoltsEl.textContent = `${tel.vb.toFixed(2)}V`;
          battVoltsEl.className = 'lcd-tx-batt';
          if (tel.vb < 7.0) {
            battVoltsEl.classList.add('critical');
          } else if (tel.vb < 7.4) {
            battVoltsEl.classList.add('warning');
          } else {
            battVoltsEl.classList.add('nominal');
          }
        }
        if (tel.vpct !== undefined) {
          battPctEl.textContent = `${tel.vpct}%`;
        }
      } else {
        linkAgeEl.textContent = '--- ms';
        linkAgeEl.removeAttribute('data-warning');
        flapHzEl.textContent = '0.0 Hz';
        steerBiasEl.textContent = '0.00';
      }

      // 3. Inputs & Readouts
      const thrPct = Math.round(state.throttleInput * 100);
      thrReadout.textContent = `${thrPct}%`;
      leftBezel.setAttribute('aria-valuenow', String(thrPct));

      const strVal = state.steerInput.toFixed(2);
      strReadout.textContent = strVal;
      rightBezel.setAttribute('aria-valuenow', String(Math.round(state.steerInput * 100)));

      // 4. Live 4-Channel Servo Monitor
      const thrChannel = Math.round(state.throttleInput * 100);
      ch1Bar.style.width = `${thrChannel}%`;
      ch1Val.textContent = `${thrChannel}%`;

      const steerChannel = state.steerInput; // -1 .. +1
      const steerBarWidth = Math.abs(steerChannel) * 50;
      ch2Bar.style.width = `${steerBarWidth}%`;
      ch2Bar.style.left = steerChannel >= 0 ? '50%' : `${50 - steerBarWidth}%`;
      ch2Val.textContent = steerChannel.toFixed(2);

      // Estimate W-L and W-R deflections for servo monitor
      const centerAngle = 90;
      const leftDefl = currentFlightState === 'ARMED' ? Math.round(centerAngle + state.throttleInput * 40 * (1 + state.steerInput * 0.25)) : centerAngle;
      const rightDefl = currentFlightState === 'ARMED' ? Math.round(centerAngle + state.throttleInput * 40 * (1 - state.steerInput * 0.25)) : centerAngle;
      ch3Bar.style.width = `${(leftDefl / 180) * 100}%`;
      ch3Val.textContent = `${leftDefl}°`;
      ch4Bar.style.width = `${(rightDefl / 180) * 100}%`;
      ch4Val.textContent = `${rightDefl}°`;
    },

    destroy(): void {
      cancelArmHold();
      stopFlightTimer();
      releaseWakeLock();
      if (releaseLockTimeout !== null) {
        clearTimeout(releaseLockTimeout);
      }
    },
  };
}

export default createFlightDeckUI;
