#!/usr/bin/env python3
"""Deterministic tests for sync_webapp.py.

Contract C4 verification:
- Missing source directory raises error / exits non-zero.
- Happy path with mock files (HTML, JS, CSS, subdirectories): verify all files gzipped,
  relative paths preserved, exits 0.
- Pre-gzipped files: verify copied without double-compression.
- Over-budget rejection: mock files that exceed 153600 bytes after gzip causes exit non-zero.
- Verifies total byte calculation accuracy.
- Target directory cleaning and budget boundary conditions.
"""

from __future__ import annotations

import gzip
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

# Ensure firmware/scripts can import sync_webapp directly
SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import sync_webapp
from sync_webapp import (
    BudgetExceededError,
    MAX_BUDGET_BYTES,
    SourceDirNotFoundError,
    compute_total_size,
    resolve_dist_path,
    resolve_target_path,
    sync_webapp as run_sync,
)

SYNC_SCRIPT_PATH = SCRIPT_DIR / "sync_webapp.py"


class TestSyncWebappLibrary(unittest.TestCase):
    """Unit tests for sync_webapp library functions."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.base_path = Path(self.temp_dir.name)
        self.mock_dist = self.base_path / "dist"
        self.mock_target = self.base_path / "data"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def test_missing_source_directory_raises_error(self) -> None:
        """Missing dist directory must raise SourceDirNotFoundError."""
        non_existent = self.base_path / "missing_dist"
        with self.assertRaises(SourceDirNotFoundError) as ctx:
            run_sync(non_existent, self.mock_target)
        self.assertIn("does not exist", str(ctx.exception))

    def test_source_directory_is_file_raises_error(self) -> None:
        """If source dist is a regular file, raise SourceDirNotFoundError."""
        file_dist = self.base_path / "dist_file.txt"
        file_dist.write_text("not a directory")
        with self.assertRaises(SourceDirNotFoundError) as ctx:
            run_sync(file_dist, self.mock_target)
        self.assertIn("not a directory", str(ctx.exception))

    def test_happy_path_mock_files_decompression_and_paths(self) -> None:
        """Mock files across nested directories must be properly gzipped with paths preserved."""
        self.mock_dist.mkdir(parents=True)
        (self.mock_dist / "assets").mkdir(parents=True)
        (self.mock_dist / "nested" / "deep").mkdir(parents=True)

        files = {
            "index.html": b"<!DOCTYPE html><html><body><h1>Troidrone Controller</h1></body></html>",
            "assets/index-D7h2k9.js": b"console.log('troidrone startup'); export const ready = true;",
            "assets/style-A1b2c3.css": b"body { margin: 0; background-color: #121212; color: #fff; }",
            "nested/deep/manifest.json": b'{"name": "troidrone", "short_name": "troidrone"}',
        }

        for rel_path, content in files.items():
            full_path = self.mock_dist / rel_path
            full_path.parent.mkdir(parents=True, exist_ok=True)
            full_path.write_bytes(content)

        total_size = run_sync(self.mock_dist, self.mock_target)

        self.assertGreater(total_size, 0)
        self.assertLess(total_size, MAX_BUDGET_BYTES)

        # Verify each file is gzipped with .gz extension in target, preserving relative paths
        for rel_path, expected_content in files.items():
            target_file = self.mock_target / f"{rel_path}.gz"
            self.assertTrue(
                target_file.is_file(),
                f"Expected gzipped target file missing: {target_file}",
            )
            # Uncompressed file must NOT exist in target
            uncompressed_target = self.mock_target / rel_path
            self.assertFalse(
                uncompressed_target.exists(),
                f"Uncompressed file must not exist in target: {uncompressed_target}",
            )
            # Decompress and verify content matches original exactly
            decompressed_data = gzip.decompress(target_file.read_bytes())
            self.assertEqual(
                decompressed_data,
                expected_content,
                f"Decompressed content does not match for {rel_path}",
            )

    def test_pre_gzipped_files_not_double_compressed(self) -> None:
        """Pre-gzipped files in dist must be copied directly without double compression."""
        self.mock_dist.mkdir(parents=True)
        (self.mock_dist / "assets").mkdir(parents=True)

        raw_js = b"console.log('already compressed asset');"
        compressed_js = gzip.compress(raw_js, compresslevel=9)
        pre_gz_path = self.mock_dist / "assets" / "vendor.js.gz"
        pre_gz_path.write_bytes(compressed_js)

        raw_html = b"<!DOCTYPE html><html><body>Plain</body></html>"
        plain_html_path = self.mock_dist / "index.html"
        plain_html_path.write_bytes(raw_html)

        total_size = run_sync(self.mock_dist, self.mock_target)

        target_vendor = self.mock_target / "assets" / "vendor.js.gz"
        target_vendor_double = self.mock_target / "assets" / "vendor.js.gz.gz"

        self.assertTrue(target_vendor.is_file(), "Target vendor.js.gz must exist")
        self.assertFalse(
            target_vendor_double.exists(),
            "Double-gzipped file vendor.js.gz.gz must not exist",
        )

        # Raw bytes in target must match the pre-compressed source bytes exactly
        self.assertEqual(
            target_vendor.read_bytes(),
            compressed_js,
            "Pre-gzipped file should be copied verbatim without recompression",
        )

        # Decompressing once must yield raw uncompressed data
        self.assertEqual(gzip.decompress(target_vendor.read_bytes()), raw_js)

        # Plain html must be gzipped
        target_html = self.mock_target / "index.html.gz"
        self.assertTrue(target_html.is_file())
        self.assertEqual(gzip.decompress(target_html.read_bytes()), raw_html)

    def test_over_budget_rejection_library(self) -> None:
        """Files that exceed MAX_BUDGET_BYTES after gzip must raise BudgetExceededError."""
        self.mock_dist.mkdir(parents=True)
        # Using pseudo-random incompressible bytes ensures gzip cannot compress below 153600
        incompressible_data = os.urandom(160000)
        (self.mock_dist / "payload.bin").write_bytes(incompressible_data)

        with self.assertRaises(BudgetExceededError) as ctx:
            run_sync(self.mock_dist, self.mock_target)

        self.assertGreaterEqual(ctx.exception.total_size, MAX_BUDGET_BYTES)
        self.assertEqual(ctx.exception.budget, MAX_BUDGET_BYTES)

    def test_budget_boundary_conditions(self) -> None:
        """Verify strict boundary check: total_size >= budget fails, total_size < budget passes."""
        self.mock_dist.mkdir(parents=True)
        test_data = b"Exact boundary test content 1234567890"
        (self.mock_dist / "file.txt").write_bytes(test_data)

        # First run to find actual compressed size
        actual_size = run_sync(self.mock_dist, self.mock_target)

        # If budget equals actual_size, total_size >= budget must raise BudgetExceededError
        with self.assertRaises(BudgetExceededError):
            run_sync(self.mock_dist, self.mock_target, budget=actual_size)

        # If budget is actual_size + 1, it must succeed
        success_size = run_sync(self.mock_dist, self.mock_target, budget=actual_size + 1)
        self.assertEqual(success_size, actual_size)

    def test_total_byte_calculation_accuracy(self) -> None:
        """Total size reported must exactly match the sum of st_size of all target files."""
        self.mock_dist.mkdir(parents=True)
        (self.mock_dist / "sub").mkdir(parents=True)

        # Create multiple files with varied content
        (self.mock_dist / "a.html").write_bytes(b"A" * 500)
        (self.mock_dist / "b.js").write_bytes(b"B" * 1200)
        (self.mock_dist / "sub" / "c.css").write_bytes(b"C" * 300)
        precompressed = gzip.compress(b"D" * 800, compresslevel=9)
        (self.mock_dist / "sub" / "d.bin.gz").write_bytes(precompressed)

        reported_size = run_sync(self.mock_dist, self.mock_target)

        # Manually compute total size
        computed_size = compute_total_size(self.mock_target)
        self.assertEqual(reported_size, computed_size)

        manual_sum = sum(f.stat().st_size for f in self.mock_target.rglob("*") if f.is_file())
        self.assertEqual(reported_size, manual_sum)

    def test_clean_existing_target_contents(self) -> None:
        """Old files and subdirectories in target directory must be cleaned before sync."""
        self.mock_target.mkdir(parents=True)
        stale_file = self.mock_target / "stale_file.js.gz"
        stale_file.write_bytes(b"stale data")
        stale_dir = self.mock_target / "stale_subdir"
        stale_dir.mkdir(parents=True)
        (stale_dir / "old.txt.gz").write_bytes(b"old data")

        self.mock_dist.mkdir(parents=True)
        (self.mock_dist / "fresh.html").write_bytes(b"Fresh content")

        run_sync(self.mock_dist, self.mock_target)

        self.assertFalse(stale_file.exists(), "Stale file must be removed")
        self.assertFalse(stale_dir.exists(), "Stale directory must be removed")
        self.assertTrue((self.mock_target / "fresh.html.gz").is_file(), "Fresh file must exist")

    def test_empty_dist_directory(self) -> None:
        """An empty dist directory produces an empty target with total size 0."""
        self.mock_dist.mkdir(parents=True)
        total_size = run_sync(self.mock_dist, self.mock_target)
        self.assertEqual(total_size, 0)
        self.assertTrue(self.mock_target.is_dir())
        self.assertEqual(list(self.mock_target.iterdir()), [])


class TestSyncWebappCLI(unittest.TestCase):
    """End-to-end CLI integration tests running sync_webapp.py via subprocess."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.base_path = Path(self.temp_dir.name)
        self.mock_dist = self.base_path / "dist"
        self.mock_target = self.base_path / "data"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def run_cli(self, args: list[str]) -> subprocess.CompletedProcess[str]:
        """Run sync_webapp.py in a subprocess with current Python interpreter."""
        cmd = [sys.executable, str(SYNC_SCRIPT_PATH)] + args
        return subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def test_cli_missing_source_directory_exits_nonzero(self) -> None:
        """CLI must exit non-zero and print error to stderr if dist does not exist."""
        non_existent = self.base_path / "does_not_exist"
        result = self.run_cli(["--dist", str(non_existent), "--target", str(self.mock_target)])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not exist", result.stderr.lower())

    def test_cli_happy_path(self) -> None:
        """CLI happy path must compress assets, print total size and exit 0."""
        self.mock_dist.mkdir(parents=True)
        (self.mock_dist / "assets").mkdir(parents=True)
        (self.mock_dist / "index.html").write_text("<!DOCTYPE html><html>Hello</html>")
        (self.mock_dist / "assets" / "app.js").write_text("console.log('app');")
        (self.mock_dist / "assets" / "theme.css").write_text("body { color: red; }")

        result = self.run_cli(["--dist", str(self.mock_dist), "--target", str(self.mock_target)])
        self.assertEqual(result.returncode, 0, f"CLI failed: {result.stderr}")
        self.assertIn("Total webapp asset size:", result.stdout)
        self.assertIn(f"budget: {MAX_BUDGET_BYTES} bytes", result.stdout)

        # Parse reported size from stdout
        match = re.search(r"Total webapp asset size: (\d+) bytes", result.stdout)
        self.assertIsNotNone(match, "Output format did not match expected regex")
        reported_size = int(match.group(1))

        # Check files exist in target
        self.assertTrue((self.mock_target / "index.html.gz").is_file())
        self.assertTrue((self.mock_target / "assets" / "app.js.gz").is_file())
        self.assertTrue((self.mock_target / "assets" / "theme.css.gz").is_file())

        actual_size = sum(f.stat().st_size for f in self.mock_target.rglob("*") if f.is_file())
        self.assertEqual(reported_size, actual_size)

    def test_cli_over_budget_rejection(self) -> None:
        """CLI must exit non-zero and report budget error when payload >= 153600 bytes."""
        self.mock_dist.mkdir(parents=True)
        # 160 KB of uncompressible random bytes
        (self.mock_dist / "large_payload.bin").write_bytes(os.urandom(160000))

        result = self.run_cli(["--dist", str(self.mock_dist), "--target", str(self.mock_target)])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("budget", result.stderr.lower())
        self.assertIn(str(MAX_BUDGET_BYTES), result.stderr)

    def test_cli_help_flag(self) -> None:
        """CLI --help flag prints usage and exits 0."""
        result = self.run_cli(["--help"])
        self.assertEqual(result.returncode, 0)
        self.assertIn("--dist", result.stdout)
        self.assertIn("--target", result.stdout)


class TestSyncWebappPathResolution(unittest.TestCase):
    """Test resolution of default paths."""

    def test_explicit_paths_resolved(self) -> None:
        """Explicitly passed paths should resolve to absolute Paths."""
        p_dist = resolve_dist_path("/tmp/custom_dist")
        self.assertEqual(p_dist, Path("/tmp/custom_dist").resolve())

        p_target = resolve_target_path("/tmp/custom_target")
        self.assertEqual(p_target, Path("/tmp/custom_target").resolve())

    def test_default_paths_exist(self) -> None:
        """Default target path resolves to firmware/data without errors."""
        default_target = resolve_target_path(None)
        self.assertTrue(default_target.name == "data")

        default_dist = resolve_dist_path(None)
        self.assertTrue(default_dist.name == "dist")


if __name__ == "__main__":
    unittest.main()
