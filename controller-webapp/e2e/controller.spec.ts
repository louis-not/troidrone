import { expect, test } from '@playwright/test';
import { createMockEspServer, type MockEspServer } from '../tools/mock-esp';

test.describe('Flight Deck Controller E2E', () => {
  let mockEsp: MockEspServer;
  let espPort: number;

  test.beforeEach(async () => {
    mockEsp = createMockEspServer({ proto: 1 });
    espPort = await mockEsp.start(0);
  });

  test.afterEach(async () => {
    await mockEsp.stop();
  });

  test('happy path: arm, throttle control, dead-man switch, failsafe & recovery', async ({ page }) => {
    // 1. Navigate with ?ws= pointing to mock ESP
    await page.goto(`http://localhost:4173/?ws=ws://127.0.0.1:${espPort}/ws`);

    const stateBadge = page.getByTestId('state-badge');
    const armButton = page.getByTestId('arm-button');
    const throttleStick = page.getByTestId('throttle-stick');
    const throttleValue = page.getByTestId('throttle-value');
    const flapRate = page.getByTestId('flap-rate');
    const linkAge = page.getByTestId('link-age');

    // 2. Wait for connection and initial DISARMED state
    await expect(stateBadge).toHaveText('DISARMED', { timeout: 10000 });
    await expect(armButton).toBeEnabled();
    await expect(armButton).toHaveText('ARM DRONE');
    await expect(throttleValue).toHaveText('0%');
    await expect(flapRate).toHaveText('0.0 Hz');

    // 3. Click ARM button
    await armButton.click();
    await expect(stateBadge).toHaveText('ARMED');
    await expect(armButton).toHaveText('DISARM DRONE');

    // 4. Test vertical throttle stick drag
    const box = await throttleStick.boundingBox();
    expect(box).not.toBeNull();
    if (!box) return;

    const startX = box.x + box.width / 2;
    const startY = box.y + box.height - 15; // Bottom of stick (0%)
    const targetY = box.y + box.height * 0.35; // ~65% throttle

    await page.mouse.move(startX, startY);
    await page.mouse.down();
    await page.mouse.move(startX, targetY, { steps: 5 });

    // Verify throttle value increases above 0% while mouse is held down
    await expect(throttleValue).not.toHaveText('0%');
    // Flap rate updates into 1.0 - 4.0 Hz
    await expect(flapRate).toHaveText(/^[1-4]\.[0-9] Hz$/);

    // 5. Test DEAD-MAN SWITCH release
    await page.mouse.up();
    // Throttle snaps immediately back to 0%
    await expect(throttleValue).toHaveText('0%');
    // Flap rate returns to 0.0 Hz
    await expect(flapRate).toHaveText('0.0 Hz');

    // 6. Test simulated link loss (control frame drop)
    mockEsp.setDropControlFrames(true);

    // Wait for failsafe transition (>500 ms)
    await expect(stateBadge).toHaveText('FAILSAFE', { timeout: 4000 });
    await expect(linkAge).toHaveAttribute('data-warning', 'true');

    // Restore control frame acceptance
    mockEsp.setDropControlFrames(false);

    // 7. Test recovery from failsafe
    await expect(armButton).toHaveText('RESET FAILSAFE');
    await armButton.click();

    // Verify transition to DISARMED
    await expect(stateBadge).toHaveText('DISARMED');
    await expect(armButton).toHaveText('ARM DRONE');

    // Verify drone can be armed again
    await armButton.click();
    await expect(stateBadge).toHaveText('ARMED');
    await expect(armButton).toHaveText('DISARM DRONE');
  });

  test('protocol mismatch: proto: 2 displays PROTO MISMATCH and locks out controls', async ({ page }) => {
    // Stop default server and start a proto: 2 server
    await mockEsp.stop();
    mockEsp = createMockEspServer({ proto: 2, fw: '2.0.0-incompatible' });
    const mismatchPort = await mockEsp.start(0);

    await page.goto(`http://localhost:4173/?ws=ws://127.0.0.1:${mismatchPort}/ws`);

    const stateBadge = page.getByTestId('state-badge');
    const armButton = page.getByTestId('arm-button');

    // Verify protocol mismatch state
    await expect(stateBadge).toHaveText('PROTO MISMATCH', { timeout: 10000 });
    await expect(armButton).toBeDisabled();
    await expect(armButton).toHaveText('DISCONNECTED');
  });
});
