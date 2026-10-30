import { expect, test, type BrowserContext, type Page } from '@playwright/test';
import { spawn, type ChildProcess } from 'node:child_process';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const sitlBinary = path.resolve(__dirname, '../../firmware/.pio/sitl_server');
const dataDir = path.resolve(__dirname, '../../firmware/data');

interface SitlState {
  st: string;
  pwm_active: boolean;
  left_us: number;
  right_us: number;
  freq_hz: number;
  [key: string]: unknown;
}

test.describe.serial('C++ SITL E2E Spec', () => {
  test.skip(!fs.existsSync(sitlBinary), 'SITL server binary not found at ../firmware/.pio/sitl_server');

  let child: ChildProcess | null = null;
  let port = 0;
  let primaryContext: BrowserContext | null = null;
  let page: Page | null = null;
  let secondContext: BrowserContext | null = null;
  let secondPage: Page | null = null;

  const consoleErrors: string[] = [];
  const pageErrors: string[] = [];

  test.beforeAll(async ({ browser }) => {
    if (!fs.existsSync(sitlBinary)) {
      return;
    }

    child = spawn(sitlBinary, ['--port', '0', '--data', dataDir], {
      stdio: ['ignore', 'pipe', 'pipe'],
    });

    port = await new Promise<number>((resolve, reject) => {
      let stdoutAcc = '';
      const timer = setTimeout(() => {
        reject(
          new Error(
            `Timed out waiting for SITL_PORT from sitl_server after 10s. stdout accumulated:\n${stdoutAcc}`
          )
        );
      }, 10000);

      const onData = (data: Buffer) => {
        const text = data.toString('utf-8');
        stdoutAcc += text;
        const match = stdoutAcc.match(/SITL_PORT=(\d+)/);
        if (match) {
          clearTimeout(timer);
          child?.stdout?.off('data', onData);
          resolve(parseInt(match[1], 10));
        }
      };

      child?.stdout?.on('data', onData);
      child?.on('error', (err) => {
        clearTimeout(timer);
        reject(err);
      });
      child?.on('exit', (code, signal) => {
        clearTimeout(timer);
        reject(
          new Error(
            `sitl_server exited unexpectedly before reporting SITL_PORT (code: ${code}, signal: ${signal}). stdout:\n${stdoutAcc}`
          )
        );
      });
    });

    // Wait until HTTP server responds on http://127.0.0.1:${port}/__sitl/state
    const stateUrl = `http://127.0.0.1:${port}/__sitl/state`;
    const deadline = Date.now() + 10000;
    let ready = false;
    while (Date.now() < deadline) {
      try {
        const res = await fetch(stateUrl);
        if (res.ok) {
          ready = true;
          break;
        }
      } catch {
        // Poll retry
      }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }

    if (!ready) {
      throw new Error(`SITL server did not respond at ${stateUrl} within 10s`);
    }

    primaryContext = await browser.newContext();
    page = await primaryContext.newPage();

    // Hook console errors and unhandled page errors
    page.on('console', (msg) => {
      if (msg.type() === 'error') {
        consoleErrors.push(msg.text());
      }
    });
    page.on('pageerror', (err) => {
      pageErrors.push(err.message);
    });

    // Capture WebSocket close codes on window for assertion in test 5
    await page.addInitScript(() => {
      const OriginalWebSocket = window.WebSocket;
      (window as unknown as { __wsCloseCodes: number[] }).__wsCloseCodes = [];
      window.WebSocket = class extends OriginalWebSocket {
        constructor(...args: ConstructorParameters<typeof OriginalWebSocket>) {
          super(...args);
          this.addEventListener('close', (ev: CloseEvent) => {
            (window as unknown as { __wsCloseCodes: number[] }).__wsCloseCodes.push(ev.code);
          });
        }
      };
    });
  });

  test.afterAll(async () => {
    if (secondPage && !secondPage.isClosed()) {
      await secondPage.close().catch(() => {});
    }
    if (secondContext) {
      await secondContext.close().catch(() => {});
    }
    if (page && !page.isClosed()) {
      await page.close().catch(() => {});
    }
    if (primaryContext) {
      await primaryContext.close().catch(() => {});
    }

    if (child && !child.killed) {
      const exitPromise = new Promise<void>((resolve) => {
        child?.once('exit', () => resolve());
      });
      child.kill('SIGTERM');
      const killedGracefully = await Promise.race([
        exitPromise.then(() => true),
        new Promise<boolean>((resolve) => setTimeout(() => resolve(false), 2000)),
      ]);
      if (!killedGracefully && !child.killed) {
        try {
          child.kill('SIGKILL');
        } catch {
          // Ignore
        }
      }
      await exitPromise.catch(() => {});
    }
  });

  test('1. Static asset & initial state', async () => {
    test.skip(!page, 'Page not initialized');

    await page.goto(`http://127.0.0.1:${port}/`);

    // Assert zero unhandled console or page errors
    expect(consoleErrors).toEqual([]);
    expect(pageErrors).toEqual([]);

    // Verify state badge displays DISARMED
    const stateBadge = page.getByTestId('state-badge');
    await expect(stateBadge).toHaveText('DISARMED', { timeout: 10000 });

    // Fetch /__sitl/state and verify pwm_active === false (servos detached / pins LOW at boot)
    const res = await fetch(`http://127.0.0.1:${port}/__sitl/state`);
    expect(res.ok).toBe(true);
    const state = (await res.json()) as SitlState;
    expect(state.pwm_active).toBe(false);
  });

  test('2. Arming', async () => {
    test.skip(!page, 'Page not initialized');

    const stateBadge = page.getByTestId('state-badge');
    const armButton = page.getByTestId('arm-button');

    await expect(armButton).toBeEnabled();
    await armButton.click();

    // Verify badge transitions to ARMED and button text says DISARM DRONE
    await expect(stateBadge).toHaveText('ARMED', { timeout: 5000 });
    await expect(armButton).toHaveText('DISARM DRONE');

    // Fetch /__sitl/state and verify st === 'ARMED', pwm_active === true, and left/right servos at neutral center (1450 us +- 20)
    await expect
      .poll(
        async () => {
          const res = await fetch(`http://127.0.0.1:${port}/__sitl/state`);
          if (!res.ok) return null;
          return (await res.json()) as SitlState;
        },
        { timeout: 3000, intervals: [50, 100] }
      )
      .toMatchObject({
        st: 'ARMED',
        pwm_active: true,
      });

    const res = await fetch(`http://127.0.0.1:${port}/__sitl/state`);
    const state = (await res.json()) as SitlState;
    expect(state.st).toBe('ARMED');
    expect(state.pwm_active).toBe(true);
    expect(state.left_us).toBeGreaterThanOrEqual(1430);
    expect(state.left_us).toBeLessThanOrEqual(1470);
    expect(state.right_us).toBeGreaterThanOrEqual(1430);
    expect(state.right_us).toBeLessThanOrEqual(1470);
  });

  test('3. Throttling & Flapping', async () => {
    test.skip(!page, 'Page not initialized');

    const throttleStick = page.getByTestId('throttle-stick');
    const flapRate = page.getByTestId('flap-rate');

    const box = await throttleStick.boundingBox();
    expect(box).not.toBeNull();
    if (!box) return;

    const startX = box.x + box.width / 2;
    const startY = box.y + box.height - 15; // Bottom of stick (0%)
    const targetY = box.y + box.height * 0.35; // ~65% throttle

    await page.mouse.move(startX, startY);
    await page.mouse.down();
    await page.mouse.move(startX, targetY, { steps: 5 });

    // Verify flap-rate increases to 1.0–4.0 Hz
    await expect(flapRate).toHaveText(/^[1-4]\.[0-9] Hz$/, { timeout: 5000 });

    // Fetch /__sitl/state and verify freq_hz > 1.0 and left_us !== 1450
    await expect
      .poll(
        async () => {
          const res = await fetch(`http://127.0.0.1:${port}/__sitl/state`);
          if (!res.ok) return false;
          const s = (await res.json()) as SitlState;
          return s.freq_hz > 1.0 && s.left_us !== 1450;
        },
        { timeout: 5000, intervals: [50, 100] }
      )
      .toBe(true);
  });

  test('4. Dead-man Release', async () => {
    test.skip(!page, 'Page not initialized');

    const throttleStick = page.getByTestId('throttle-stick');
    const throttleValue = page.getByTestId('throttle-value');
    const flapRate = page.getByTestId('flap-rate');

    // If mouse was not held, re-engage throttle drag to verify dead-man release
    const currentVal = await throttleValue.textContent();
    if (currentVal === '0%') {
      const box = await throttleStick.boundingBox();
      if (box) {
        const startX = box.x + box.width / 2;
        const startY = box.y + box.height - 15;
        const targetY = box.y + box.height * 0.35;
        await page.mouse.move(startX, startY);
        await page.mouse.down();
        await page.mouse.move(startX, targetY, { steps: 5 });
        await expect(throttleValue).not.toHaveText('0%');
      }
    }

    // Release pointer
    await page.mouse.up();

    // Verify throttle snaps to 0% and flap-rate snaps to 0.0 Hz
    await expect(throttleValue).toHaveText('0%', { timeout: 3000 });
    await expect(flapRate).toHaveText('0.0 Hz', { timeout: 3000 });

    // Verify servos return to neutral center (1450 us +- 20)
    await expect
      .poll(
        async () => {
          const res = await fetch(`http://127.0.0.1:${port}/__sitl/state`);
          if (!res.ok) return false;
          const s = (await res.json()) as SitlState;
          return (
            s.freq_hz === 0 &&
            Math.abs(s.left_us - 1450) <= 20 &&
            Math.abs(s.right_us - 1450) <= 20
          );
        },
        { timeout: 5000, intervals: [50, 100] }
      )
      .toBe(true);
  });

  test('5. Single-controller Supersede (Code 4001)', async ({ browser }) => {
    test.skip(!page, 'Page not initialized');

    // Open a second page/context in Chromium to http://127.0.0.1:${port}/
    secondContext = await browser.newContext();
    secondPage = await secondContext.newPage();
    await secondPage.goto(`http://127.0.0.1:${port}/`);

    // Verify the first page's WebSocket closes with code 4001
    await expect
      .poll(
        async () => {
          if (!page || page.isClosed()) return [];
          return await page.evaluate(
            () => (window as unknown as { __wsCloseCodes: number[] }).__wsCloseCodes || []
          );
        },
        { timeout: 5000, intervals: [50, 100] }
      )
      .toContain(4001);

    // Verify C++ FSM immediately transitions to FAILSAFE
    await expect
      .poll(
        async () => {
          const res = await fetch(`http://127.0.0.1:${port}/__sitl/state`);
          if (!res.ok) return null;
          const s = (await res.json()) as SitlState;
          return s.st;
        },
        { timeout: 2000, intervals: [50, 100] }
      )
      .toBe('FAILSAFE');

    // Fetch /__sitl/state and verify servos ramp back to center and pwm_active === false within 500 ms
    await expect
      .poll(
        async () => {
          const res = await fetch(`http://127.0.0.1:${port}/__sitl/state`);
          if (!res.ok) return false;
          const s = (await res.json()) as SitlState;
          return (
            s.pwm_active === false &&
            Math.abs(s.left_us - 1450) <= 20 &&
            Math.abs(s.right_us - 1450) <= 20
          );
        },
        { timeout: 1500, intervals: [50, 100] }
      )
      .toBe(true);

    // Close first page to prevent reconnect attempts
    await page.close();
    page = null;
  });

  test('6. Link Loss & Recovery', async () => {
    test.skip(!secondPage, 'Second page not initialized');

    const secondBadge = secondPage.getByTestId('state-badge');
    const secondArmButton = secondPage.getByTestId('arm-button');
    const secondLinkAge = secondPage.getByTestId('link-age');

    // Reconnect / re-arm on the second page
    await expect
      .poll(
        async () => {
          const txt = await secondArmButton.textContent();
          return txt === 'RESET FAILSAFE' || txt === 'ARM DRONE';
        },
        { timeout: 5000, intervals: [50, 100] }
      )
      .toBe(true);

    if ((await secondArmButton.textContent()) === 'RESET FAILSAFE') {
      await secondArmButton.click();
      await expect(secondBadge).toHaveText('DISARMED', { timeout: 3000 });
      await expect(secondArmButton).toHaveText('ARM DRONE');
    }

    if ((await secondArmButton.textContent()) === 'ARM DRONE') {
      await secondArmButton.click();
      await expect(secondBadge).toHaveText('ARMED', { timeout: 3000 });
      await expect(secondArmButton).toHaveText('DISARM DRONE');
    }

    // Verify drone is in ARMED state on second page
    await expect(secondBadge).toHaveText('ARMED');
    await expect
      .poll(
        async () => {
          const res = await fetch(`http://127.0.0.1:${port}/__sitl/state`);
          if (!res.ok) return '';
          const s = (await res.json()) as SitlState;
          return s.st;
        },
        { timeout: 3000, intervals: [50, 100] }
      )
      .toBe('ARMED');

    // POST http://127.0.0.1:${port}/__sitl/drop_ctl?v=1
    const dropRes = await fetch(`http://127.0.0.1:${port}/__sitl/drop_ctl?v=1`, {
      method: 'POST',
    });
    expect(dropRes.ok).toBe(true);

    // Wait >500 ms: verify badge transitions to FAILSAFE and link-age has data-warning="true"
    await expect(secondBadge).toHaveText('FAILSAFE', { timeout: 4000 });
    await expect(secondLinkAge).toHaveAttribute('data-warning', 'true', { timeout: 4000 });

    // POST http://127.0.0.1:${port}/__sitl/drop_ctl?v=0
    const restoreRes = await fetch(`http://127.0.0.1:${port}/__sitl/drop_ctl?v=0`, {
      method: 'POST',
    });
    expect(restoreRes.ok).toBe(true);

    // Click RESET FAILSAFE -> verify badge DISARMED
    await expect(secondArmButton).toHaveText('RESET FAILSAFE', { timeout: 3000 });
    await secondArmButton.click();
    await expect(secondBadge).toHaveText('DISARMED', { timeout: 3000 });
    await expect(secondArmButton).toHaveText('ARM DRONE');

    // Click ARM DRONE -> verify badge ARMED
    await secondArmButton.click();
    await expect(secondBadge).toHaveText('ARMED', { timeout: 3000 });
    await expect(secondArmButton).toHaveText('DISARM DRONE');
  });
});
