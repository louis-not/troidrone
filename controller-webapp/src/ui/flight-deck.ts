/**
 * Flight Deck Single-Screen Controller UI Component.
 * Implements high-contrast dark tactical flight instrumentation.
 */

import type { ControllerState, FlightState } from '../store';

export interface FlightDeckCallbacks {
  onThrottleChange: (value: number) => void;
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
  container.className = 'flight-deck';

  // Construct UI markup
  container.innerHTML = `
    <header class="flight-deck-header">
      <div class="deck-topline">
        <div class="drone-branding">
          <span class="radar-dot" aria-hidden="true"></span>
          <span class="drone-callsign">TROIDRONE MK-1</span>
          <span class="tactical-badge">SYS:OK</span>
        </div>
        <div
          class="state-badge state-badge--disconnected"
          data-testid="state-badge"
          title="Firmware: Unknown"
          role="status"
          aria-live="polite"
        ><span class="state-badge-icon" aria-hidden="true"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><line x1="4.93" y1="4.93" x2="19.07" y2="19.07"/></svg></span><span class="state-badge-label">DISCONNECTED</span></div>
      </div>

      <div class="telemetry-bar">
        <div class="telemetry-card">
          <div class="telemetry-header">
            <svg class="telemetry-icon" aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            <span class="telemetry-label">LINK AGE</span>
          </div>
          <div class="telemetry-value-wrap">
            <span class="telemetry-val" data-testid="link-age">--- ms</span>
          </div>
        </div>

        <div class="telemetry-card">
          <div class="telemetry-header">
            <svg class="telemetry-icon" aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M13 10V3L4 14h7v7l9-11h-7z" />
            </svg>
            <span class="telemetry-label">FLAP RATE</span>
          </div>
          <div class="telemetry-value-wrap">
            <span class="telemetry-val" data-testid="flap-rate">0.0 Hz</span>
          </div>
        </div>
      </div>
    </header>

    <main class="throttle-deck">
      <div
        class="throttle-stick"
        data-testid="throttle-stick"
        role="slider"
        aria-label="Throttle"
        aria-valuemin="0"
        aria-valuemax="100"
        aria-valuenow="0"
        aria-valuetext="0%"
        aria-orientation="vertical"
        tabindex="0"
      >
        <div class="stick-scale" aria-hidden="true">
          <div class="scale-mark mark-100"><span class="scale-tick"></span><span class="scale-label">100%</span></div>
          <div class="scale-mark mark-75"><span class="scale-tick"></span><span class="scale-label">75%</span></div>
          <div class="scale-mark mark-50"><span class="scale-tick"></span><span class="scale-label">50%</span></div>
          <div class="scale-mark mark-25"><span class="scale-tick"></span><span class="scale-label">25%</span></div>
          <div class="scale-mark mark-0"><span class="scale-tick"></span><span class="scale-label">0%</span></div>
        </div>

        <div class="stick-rail" aria-hidden="true">
          <div class="stick-fill"></div>
        </div>

        <div class="stick-readout">
          <span class="readout-label">THRUST LEVEL</span>
          <span class="readout-val" data-testid="throttle-value">0%</span>
        </div>

        <div class="throttle-thumb" data-testid="throttle-thumb">
          <div class="thumb-grip" aria-hidden="true">
            <span class="grip-line"></span>
            <span class="grip-line"></span>
            <span class="grip-line"></span>
          </div>
          <div class="thumb-indicator" aria-hidden="true"></div>
        </div>
      </div>

      <div class="safety-advisory" aria-hidden="true">
        <span class="advisory-icon">⚠</span>
        <span class="advisory-text">DEAD-MAN SWITCH • RELEASE TO CUT THRUST</span>
      </div>
    </main>

    <footer class="flight-deck-footer">
      <button
        class="arm-button arm-button--disconnected"
        data-testid="arm-button"
        disabled
        type="button"
      ><span class="arm-btn-icon" aria-hidden="true"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="1" y1="1" x2="23" y2="23"/><path d="M16.72 11.06A10.94 10.94 0 0 1 19 12.55"/><path d="M5 12.55a10.94 10.94 0 0 1 5.17-2.39"/></svg></span><span class="arm-btn-label">DISCONNECTED</span></button>
    </footer>
  `;

  // Element references
  const stateBadgeEl = container.querySelector<HTMLElement>('[data-testid="state-badge"]')!;
  const linkAgeEl = container.querySelector<HTMLElement>('[data-testid="link-age"]')!;
  const flapRateEl = container.querySelector<HTMLElement>('[data-testid="flap-rate"]')!;
  const throttleStickEl = container.querySelector<HTMLElement>('[data-testid="throttle-stick"]')!;
  const throttleThumbEl = container.querySelector<HTMLElement>('[data-testid="throttle-thumb"]')!;
  const throttleValueEl = container.querySelector<HTMLElement>('[data-testid="throttle-value"]')!;
  const stickFillEl = container.querySelector<HTMLElement>('.stick-fill')!;
  const armButtonEl = container.querySelector<HTMLButtonElement>('[data-testid="arm-button"]')!;

  let isDragging = false;
  let activePointerId: number | null = null;
  let currentFlightState: FlightState = 'DISARMED';
  let isConnected = false;

  // Pointer position to throttle value mapping
  const updateThrottleFromPointer = (clientY: number): void => {
    const rect = throttleStickEl.getBoundingClientRect();
    const height = rect.height;
    if (height <= 0) return;

    // Linear mapping: bottom (rect.bottom) = 0.0, top (rect.top) = 1.0
    const fraction = (rect.bottom - clientY) / height;
    const clamped = Math.max(0, Math.min(1, fraction));
    callbacks.onThrottleChange(clamped);
  };

  // Immediate dead-man switch snap to 0.0
  const snapToZero = (pointerId?: number): void => {
    if (pointerId !== undefined && activePointerId !== null && pointerId !== activePointerId) {
      return;
    }
    if (activePointerId !== null && throttleStickEl.hasPointerCapture(activePointerId)) {
      try {
        throttleStickEl.releasePointerCapture(activePointerId);
      } catch {
        // ignore capture release error
      }
    }
    isDragging = false;
    activePointerId = null;
    throttleStickEl.classList.remove('throttle-stick--active');

    // Dead-man safety: reset throttle input in store immediately
    callbacks.onThrottleReset();

    // Immediate DOM snap
    updateThrottleVisuals(0);
  };

  const handlePointerDown = (e: PointerEvent): void => {
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    activePointerId = e.pointerId;
    isDragging = true;
    throttleStickEl.classList.add('throttle-stick--active');
    try {
      throttleStickEl.setPointerCapture(e.pointerId);
    } catch {
      // ignore
    }
    updateThrottleFromPointer(e.clientY);
  };

  const handlePointerMove = (e: PointerEvent): void => {
    if (isDragging && (activePointerId === null || activePointerId === e.pointerId)) {
      updateThrottleFromPointer(e.clientY);
    }
  };

  const handlePointerUp = (e: PointerEvent): void => {
    snapToZero(e.pointerId);
  };

  const handlePointerCancel = (e: PointerEvent): void => {
    snapToZero(e.pointerId);
  };

  const handleLostPointerCapture = (e: PointerEvent): void => {
    snapToZero(e.pointerId);
  };

  // Keyboard accessibility for throttle stick
  const handleKeyDown = (e: KeyboardEvent): void => {
    const current = Number(throttleStickEl.getAttribute('aria-valuenow') ?? 0) / 100;
    let next = current;
    if (e.key === 'ArrowUp') {
      next = Math.min(1, current + 0.05);
      e.preventDefault();
    } else if (e.key === 'ArrowDown') {
      next = Math.max(0, current - 0.05);
      e.preventDefault();
    } else if (e.key === 'PageUp') {
      next = Math.min(1, current + 0.2);
      e.preventDefault();
    } else if (e.key === 'PageDown') {
      next = Math.max(0, current - 0.2);
      e.preventDefault();
    } else if (e.key === 'Home') {
      next = 0;
      e.preventDefault();
    } else if (e.key === 'End') {
      next = 1;
      e.preventDefault();
    }
    if (next !== current) {
      callbacks.onThrottleChange(next);
    }
  };

  const handleKeyUp = (): void => {
    // Dead-man safety: releasing key snaps back to 0.0
    snapToZero();
  };

  const handleBlur = (): void => {
    snapToZero();
  };

  // Window-level safety hooks
  const handleWindowPointerUp = (e: PointerEvent): void => {
    if (isDragging) {
      snapToZero(e.pointerId);
    }
  };

  const handleWindowBlur = (): void => {
    if (isDragging) {
      snapToZero();
    }
  };

  // Arm button click handler
  const handleArmButtonClick = (): void => {
    if (!isConnected) return;
    if (currentFlightState === 'DISARMED') {
      callbacks.onArmAction(true);
    } else if (currentFlightState === 'ARMED') {
      callbacks.onArmAction(false);
    } else if (currentFlightState === 'FAILSAFE') {
      callbacks.onArmAction(false);
    }
  };

  // Attach event listeners
  throttleStickEl.addEventListener('pointerdown', handlePointerDown);
  throttleStickEl.addEventListener('pointermove', handlePointerMove);
  throttleStickEl.addEventListener('pointerup', handlePointerUp);
  throttleStickEl.addEventListener('pointercancel', handlePointerCancel);
  throttleStickEl.addEventListener('lostpointercapture', handleLostPointerCapture);
  throttleStickEl.addEventListener('keydown', handleKeyDown);
  throttleStickEl.addEventListener('keyup', handleKeyUp);
  throttleStickEl.addEventListener('blur', handleBlur);

  window.addEventListener('pointerup', handleWindowPointerUp);
  window.addEventListener('pointercancel', handleWindowPointerUp);
  window.addEventListener('blur', handleWindowBlur);

  armButtonEl.addEventListener('click', handleArmButtonClick);

  // Visual updater for throttle stick
  const updateThrottleVisuals = (thr: number): void => {
    const pct = Math.round(thr * 100);
    throttleValueEl.textContent = `${pct}%`;
    throttleStickEl.setAttribute('aria-valuenow', String(pct));
    throttleStickEl.setAttribute('aria-valuetext', `${pct}%`);

    // Thumb positioning with 52px thumb height:
    // When thr = 0: bottom is 0px
    // When thr = 1: bottom is 100% - 52px
    // Center of thumb linearly aligns with throttle fraction
    throttleThumbEl.style.bottom = `calc(${thr * 100}% - ${thr * 52}px)`;
    stickFillEl.style.height = `${thr * 100}%`;
  };

  // SVG Icons for state badges
  const icons = {
    disarmed: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path stroke-linecap="round" stroke-linejoin="round" d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><circle cx="12" cy="11" r="3"/></svg>`,
    armed: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path stroke-linecap="round" stroke-linejoin="round" d="M13 10V3L4 14h7v7l9-11h-7z"/></svg>`,
    failsafe: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path stroke-linecap="round" stroke-linejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z"/></svg>`,
    disconnected: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><line x1="4.93" y1="4.93" x2="19.07" y2="19.07"/></svg>`,
    protoMismatch: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="7.86 2 16.14 2 22 7.86 22 16.14 16.14 22 7.86 22 2 16.14 2 7.86 7.86 2"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>`,
    connecting: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="spin"><path stroke-linecap="round" stroke-linejoin="round" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"/></svg>`,
    armBtnDisarm: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><polygon points="7.86 2 16.14 2 22 7.86 22 16.14 16.14 22 7.86 22 2 16.14 2 7.86 7.86 2"/><line x1="9" y1="9" x2="15" y2="15"/><line x1="15" y1="9" x2="9" y2="15"/></svg>`,
    armBtnArm: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path stroke-linecap="round" stroke-linejoin="round" d="M13 10V3L4 14h7v7l9-11h-7z"/></svg>`,
    armBtnLockout: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path stroke-linecap="round" stroke-linejoin="round" d="M12 4v16m0 0l-6-6m6 6l6-6"/></svg>`,
    armBtnReset: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path stroke-linecap="round" stroke-linejoin="round" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"/></svg>`,
    armBtnDisconnected: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="1" y1="1" x2="23" y2="23"/><path d="M16.72 11.06A10.94 10.94 0 0 1 19 12.55"/><path d="M5 12.55a10.94 10.94 0 0 1 5.17-2.39"/></svg>`,
  };

  // Main reactive update function
  const update = (state: ControllerState): void => {
    isConnected = state.connState === 'CONNECTED';
    currentFlightState = state.telemetry?.st ?? 'DISARMED';

    // 1. State Badge Update
    const fwTitle = state.fw ? `Firmware: ${state.fw}` : 'Firmware: Unknown';
    stateBadgeEl.setAttribute('title', fwTitle);

    // Remove old state classes
    stateBadgeEl.classList.remove(
      'state-badge--disconnected',
      'state-badge--proto-mismatch',
      'state-badge--connecting',
      'state-badge--disarmed',
      'state-badge--armed',
      'state-badge--failsafe'
    );

    let badgeText = 'DISCONNECTED';
    let badgeIcon = icons.disconnected;
    let badgeClass = 'state-badge--disconnected';

    if (state.connState === 'DISCONNECTED') {
      badgeText = 'DISCONNECTED';
      badgeIcon = icons.disconnected;
      badgeClass = 'state-badge--disconnected';
    } else if (state.connState === 'PROTO_MISMATCH') {
      badgeText = 'PROTO MISMATCH';
      badgeIcon = icons.protoMismatch;
      badgeClass = 'state-badge--proto-mismatch';
    } else if (state.connState === 'CONNECTING') {
      badgeText = 'CONNECTING';
      badgeIcon = icons.connecting;
      badgeClass = 'state-badge--connecting';
    } else if (state.connState === 'CONNECTED') {
      if (currentFlightState === 'ARMED') {
        badgeText = 'ARMED';
        badgeIcon = icons.armed;
        badgeClass = 'state-badge--armed';
      } else if (currentFlightState === 'FAILSAFE') {
        badgeText = 'FAILSAFE';
        badgeIcon = icons.failsafe;
        badgeClass = 'state-badge--failsafe';
      } else {
        badgeText = 'DISARMED';
        badgeIcon = icons.disarmed;
        badgeClass = 'state-badge--disarmed';
      }
    }

    stateBadgeEl.classList.add(badgeClass);
    // Strict innerHTML without interstitial whitespace ensures textContent === badgeText exactly
    stateBadgeEl.innerHTML = `<span class="state-badge-icon" aria-hidden="true">${badgeIcon}</span><span class="state-badge-label">${badgeText}</span>`;

    // 2. Link Age Readout Update
    if (state.connState === 'CONNECTED' && state.telemetry !== null) {
      const age = state.telemetry.age;
      linkAgeEl.textContent = `${age} ms`;
      if (age > 200) {
        linkAgeEl.setAttribute('data-warning', 'true');
      } else {
        linkAgeEl.removeAttribute('data-warning');
      }
    } else {
      linkAgeEl.textContent = '--- ms';
      linkAgeEl.removeAttribute('data-warning');
    }

    // 3. Flap Rate Readout Update
    if (state.connState === 'CONNECTED' && state.telemetry !== null) {
      flapRateEl.textContent = `${state.telemetry.f.toFixed(1)} Hz`;
    } else {
      flapRateEl.textContent = '0.0 Hz';
    }

    // 4. Throttle Stick Position & Readout Update
    updateThrottleVisuals(state.throttleInput);

    // 5. Arm / Disarm Button Update
    armButtonEl.classList.remove(
      'arm-button--disconnected',
      'arm-button--lockout',
      'arm-button--arm',
      'arm-button--armed',
      'arm-button--failsafe'
    );

    let btnDisabled = true;
    let btnText = 'DISCONNECTED';
    let btnIcon = icons.armBtnDisconnected;
    let btnClass = 'arm-button--disconnected';

    if (state.connState !== 'CONNECTED') {
      btnDisabled = true;
      btnText = 'DISCONNECTED';
      btnIcon = icons.armBtnDisconnected;
      btnClass = 'arm-button--disconnected';
    } else {
      // connState === 'CONNECTED'
      if (currentFlightState === 'DISARMED') {
        if (state.throttleInput >= 0.05) {
          // Safety Lockout: cannot arm drone if throttle is raised
          btnDisabled = true;
          btnText = 'LOWER THROTTLE TO ARM';
          btnIcon = icons.armBtnLockout;
          btnClass = 'arm-button--lockout';
        } else {
          // Safe to arm
          btnDisabled = false;
          btnText = 'ARM DRONE';
          btnIcon = icons.armBtnArm;
          btnClass = 'arm-button--arm';
        }
      } else if (currentFlightState === 'ARMED') {
        btnDisabled = false;
        btnText = 'DISARM DRONE';
        btnIcon = icons.armBtnDisarm;
        btnClass = 'arm-button--armed';
      } else if (currentFlightState === 'FAILSAFE') {
        btnDisabled = false;
        btnText = 'RESET FAILSAFE';
        btnIcon = icons.armBtnReset;
        btnClass = 'arm-button--failsafe';
      }
    }

    armButtonEl.disabled = btnDisabled;
    armButtonEl.classList.add(btnClass);
    // Strict innerHTML without interstitial whitespace ensures textContent === btnText exactly
    armButtonEl.innerHTML = `<span class="arm-btn-icon" aria-hidden="true">${btnIcon}</span><span class="arm-btn-label">${btnText}</span>`;
  };

  const destroy = (): void => {
    throttleStickEl.removeEventListener('pointerdown', handlePointerDown);
    throttleStickEl.removeEventListener('pointermove', handlePointerMove);
    throttleStickEl.removeEventListener('pointerup', handlePointerUp);
    throttleStickEl.removeEventListener('pointercancel', handlePointerCancel);
    throttleStickEl.removeEventListener('lostpointercapture', handleLostPointerCapture);
    throttleStickEl.removeEventListener('keydown', handleKeyDown);
    throttleStickEl.removeEventListener('keyup', handleKeyUp);
    throttleStickEl.removeEventListener('blur', handleBlur);

    window.removeEventListener('pointerup', handleWindowPointerUp);
    window.removeEventListener('pointercancel', handleWindowPointerUp);
    window.removeEventListener('blur', handleWindowBlur);

    armButtonEl.removeEventListener('click', handleArmButtonClick);
    container.remove();
  };

  return {
    element: container,
    update,
    destroy,
  };
}
