import { expect, test } from '@playwright/test';
import { createMockEspServer, type MockEspServer } from '../tools/mock-esp';

test.describe('Flight Deck Controller E2E', () => {
  let mockEsp: MockEspServer;
  let espPort: number;

  test.beforeEach(async () => {
    mockEsp = createMockEspServer({ proto: 2 });
    espPort = await mockEsp.start(0);
  });

  test.afterEach(async () => {
    await mockEsp.stop();
  });

  test('happy path: arm, dual gimbals, dead-man snap, switches, failsafe & recovery', async ({ page }) => {
    // 1. Navigate with ?ws= pointing to mock ESP
    await page.goto(`http://localhost:4173/?ws=ws://127.0.0.1:${espPort}/ws`);

    const stateBadge = page.getByTestId('state-badge');
    const armButton = page.getByTestId('arm-button');
    const throttleStick = page.getByTestId('throttle-stick');
    const steerStick = page.getByTestId('steer-stick');
    const throttleValue = page.getByTestId('throttle-value');
    const flapRate = page.getByTestId('flap-rate');
    const linkAge = page.getByTestId('link-age');
    const battVolts = page.getByTestId('batt-volts');
    const ch1Bar = page.getByTestId('ch1-bar');
    const ch2Bar = page.getByTestId('ch2-bar');
    const switchSa = page.getByTestId('switch-sa');
    const switchSd = page.getByTestId('switch-sd');

    // 2. Wait for connection and initial DISARMED state
    await expect(stateBadge).toHaveText('DISARMED', { timeout: 10000 });
    await expect(armButton).toBeEnabled();
    await expect(armButton).toHaveText(/ARM DRONE/);
    await expect(throttleValue).toHaveText('0%');
    await expect(flapRate).toHaveText('0.0 Hz');
    await expect(battVolts).toContainText('7.82');

    // 3. Arm drone via armButton click
    await armButton.click();
    await expect(stateBadge).toHaveText('ARMED');
    await expect(armButton).toHaveText(/DISARM DRONE/);

    // 4. Test Left Gimbal vertical throttle drag
    const throttleBox = await throttleStick.boundingBox();
    expect(throttleBox).not.toBeNull();
    if (!throttleBox) return;

    const startX = throttleBox.x + throttleBox.width / 2;
    const startY = throttleBox.y + throttleBox.height - 15; // Bottom (0%)
    const targetY = throttleBox.y + throttleBox.height * 0.35; // ~65% throttle

    await page.mouse.move(startX, startY);
    await page.mouse.down();
    await page.mouse.move(startX, targetY, { steps: 5 });

    // Verify throttle value increases and flap rate updates into 1.0 - 4.0 Hz
    await expect(throttleValue).not.toHaveText('0%');
    await expect(flapRate).toHaveText(/^[1-4]\.[0-9] Hz$/);

    // Verify CH1 servo monitor bar reflects throttle
    await expect(ch1Bar).not.toHaveCSS('width', '0px');

    // 5. Test DEAD-MAN SWITCH release: snap to 0 with stop frame dispatch
    await page.mouse.up();
    await expect(throttleValue).toHaveText('0%');
    await expect(flapRate).toHaveText('0.0 Hz');
    expect(mockEsp.getState().thr).toBe(0.0);

    // 6. Test Right Gimbal horizontal steering drag & spring return
    const steerBox = await steerStick.boundingBox();
    expect(steerBox).not.toBeNull();
    if (!steerBox) return;

    const steerCenterX = steerBox.x + steerBox.width / 2;
    const steerCenterY = steerBox.y + steerBox.height / 2;
    const steerRightX = steerBox.x + steerBox.width * 0.85;

    await page.mouse.move(steerCenterX, steerCenterY);
    await page.mouse.down();
    await page.mouse.move(steerRightX, steerCenterY, { steps: 5 });

    // Verify CH2 steering bar deflects
    await expect(ch2Bar).not.toHaveCSS('width', '0px');

    // Release steering gimbal -> spring returns to center
    await page.mouse.up();
    const steerReadout = page.locator('#lcd-steer-bias');
    await expect(steerReadout).toHaveText('0.00');

    // 7. Test Emergency Motor Cut via Switch SD
    await switchSd.click();
    await expect(stateBadge).toHaveText('DISARMED');
    await expect(armButton).toHaveText(/ARM DRONE/);

    // 8. Test Toggle Arming via Switch SA
    await switchSa.click();
    await expect(stateBadge).toHaveText('ARMED');
    await expect(armButton).toHaveText(/DISARM DRONE/);

    // 9. Test simulated link loss (drop control frames)
    mockEsp.setDropControlFrames(true);

    // Wait for failsafe transition (>500 ms)
    await expect(stateBadge).toHaveText('FAILSAFE', { timeout: 4000 });
    await expect(linkAge).toHaveAttribute('data-warning', 'true');

    // Restore control frames and recover
    mockEsp.setDropControlFrames(false);
    await expect(armButton).toHaveText(/RESET FAILSAFE/);
    await armButton.click();

    // Verify transition to DISARMED
    await expect(stateBadge).toHaveText('DISARMED');
    await expect(armButton).toHaveText(/ARM DRONE/);
  });

  test('battery telemetry levels and LOWBATT latch lockout', async ({ page }) => {
    await page.goto(`http://localhost:4173/?ws=ws://127.0.0.1:${espPort}/ws`);

    const stateBadge = page.getByTestId('state-badge');
    const armButton = page.getByTestId('arm-button');
    const battVolts = page.getByTestId('batt-volts');
    await expect(stateBadge).toHaveText('DISARMED', { timeout: 10000 });

    // 1. WARNING level (< 7.4V)
    await fetch(`http://127.0.0.1:${espPort}/__mock/vbat?v=7.20`, { method: 'POST' });
    await expect(battVolts).toHaveClass(/warning/, { timeout: 3000 });

    // 2. CRITICAL level (< 7.0V)
    await fetch(`http://127.0.0.1:${espPort}/__mock/vbat?v=6.80`, { method: 'POST' });
    await expect(battVolts).toHaveClass(/critical/, { timeout: 3000 });

    // 3. CUTOFF level (< 6.4V sustained > 2.0s)
    await fetch(`http://127.0.0.1:${espPort}/__mock/vbat?v=6.30`, { method: 'POST' });

    // Wait > 2.0s for cutoff latch
    await expect(stateBadge).toHaveText('LOWBATT', { timeout: 5000 });
    await expect(armButton).toBeDisabled();
    await expect(armButton).toHaveText(/LOW BATTERY/);
  });

  test('protocol mismatch: invalid proto: 99 displays PROTO MISMATCH and locks out controls', async ({ page }) => {
    // Stop default server and start an invalid proto: 99 server
    await mockEsp.stop();
    mockEsp = createMockEspServer({ proto: 99, fw: '99.0.0-incompatible' });
    const mismatchPort = await mockEsp.start(0);

    await page.goto(`http://localhost:4173/?ws=ws://127.0.0.1:${mismatchPort}/ws`);

    const stateBadge = page.getByTestId('state-badge');
    const armButton = page.getByTestId('arm-button');

    // Verify protocol mismatch state
    await expect(stateBadge).toHaveText('PROTO MISMATCH', { timeout: 10000 });
    await expect(armButton).toBeDisabled();
    await expect(armButton).toHaveText(/PROTO MISMATCH/);
  });
});
