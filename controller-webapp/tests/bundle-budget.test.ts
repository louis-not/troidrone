import { execSync } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';
import { beforeAll, describe, expect, it } from 'vitest';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const projectRoot = resolve(__dirname, '..');
const distDir = resolve(projectRoot, 'dist');

const BUDGET_BYTES = 153600; // 150 KB

function getFilesRecursively(dir: string): string[] {
  const entries = readdirSync(dir, { withFileTypes: true });
  const files: string[] = [];
  for (const entry of entries) {
    const fullPath = join(dir, entry.name);
    if (entry.isDirectory()) {
      files.push(...getFilesRecursively(fullPath));
    } else if (entry.isFile()) {
      files.push(fullPath);
    }
  }
  return files;
}

describe('Production Bundle Budget', () => {
  beforeAll(() => {
    if (!existsSync(distDir) || !existsSync(join(distDir, 'index.html'))) {
      execSync('npm run build', { cwd: projectRoot, stdio: 'pipe' });
    }
  });

  it('contains dist/index.html and assets', () => {
    expect(existsSync(join(distDir, 'index.html'))).toBe(true);
    const files = getFilesRecursively(distDir);
    const hasJs = files.some((f) => f.endsWith('.js'));
    const hasCss = files.some((f) => f.endsWith('.css'));
    expect(hasJs).toBe(true);
    expect(hasCss).toBe(true);
  });

  it('keeps total gzipped production bundle size under 150 KB (153,600 bytes)', () => {
    const files = getFilesRecursively(distDir);
    expect(files.length).toBeGreaterThan(0);

    let totalRawBytes = 0;
    let totalGzipBytes = 0;
    const fileStats: { path: string; raw: number; gzip: number }[] = [];

    for (const filePath of files) {
      const content = readFileSync(filePath);
      const rawSize = statSync(filePath).size;
      const gzipped = gzipSync(content);
      const gzipSize = gzipped.length;

      totalRawBytes += rawSize;
      totalGzipBytes += gzipSize;

      fileStats.push({
        path: filePath.replace(distDir, ''),
        raw: rawSize,
        gzip: gzipSize,
      });
    }

    // Verify budget
    expect(totalGzipBytes).toBeLessThan(BUDGET_BYTES);
    expect(totalRawBytes).toBeGreaterThan(0);
  });
});
