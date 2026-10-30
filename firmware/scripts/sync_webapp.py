#!/usr/bin/env python3
"""Sync web app distribution assets to firmware LittleFS data directory as gzipped files.

Contract C4:
- Reads web app production static bundle from `../controller-webapp/dist/`.
- Compresses files using gzip level 9 into `firmware/data/` preserving relative directory structure.
- Pre-gzipped files (*.gz) are copied directly without double-compression.
- Computes total gzipped payload size.
- Fails with non-zero exit code if dist is missing or total size >= 153,600 bytes (150 KB).
"""

from __future__ import annotations

import argparse
import gzip
import os
from pathlib import Path
import shutil
import sys
from typing import Sequence

MAX_BUDGET_BYTES = 153600  # 150 KB


class SyncWebappError(Exception):
    """Base exception for web app asset synchronization errors."""


class SourceDirNotFoundError(SyncWebappError):
    """Raised when the source distribution directory does not exist or is invalid."""


class BudgetExceededError(SyncWebappError):
    """Raised when the total compressed asset size reaches or exceeds the budget."""

    def __init__(self, total_size: int, budget: int = MAX_BUDGET_BYTES):
        super().__init__(
            f"Target assets size {total_size} bytes exceeds maximum budget of {budget} bytes (150 KB)"
        )
        self.total_size = total_size
        self.budget = budget


def resolve_dist_path(dist_arg: str | Path | None) -> Path:
    """Resolve the source distribution directory path."""
    if dist_arg is not None:
        return Path(dist_arg).resolve()

    # Candidate 1: relative to current working directory (e.g. running from firmware/)
    cwd_candidate = (Path.cwd() / "../controller-webapp/dist").resolve()
    if cwd_candidate.is_dir():
        return cwd_candidate

    # Candidate 2: relative to cwd if in repo root
    cwd_root_candidate = (Path.cwd() / "controller-webapp/dist").resolve()
    if cwd_root_candidate.is_dir():
        return cwd_root_candidate

    # Candidate 3: relative to script location (firmware/scripts/../../controller-webapp/dist)
    script_firmware_dir = Path(__file__).resolve().parent.parent
    script_candidate = (script_firmware_dir.parent / "controller-webapp" / "dist").resolve()
    if script_candidate.is_dir():
        return script_candidate

    # Default fallback relative to script's parent repo if directory exists, else cwd candidate
    if (script_firmware_dir.parent / "controller-webapp").is_dir():
        return script_candidate
    return cwd_candidate


def resolve_target_path(target_arg: str | Path | None) -> Path:
    """Resolve the target data directory path."""
    if target_arg is not None:
        return Path(target_arg).resolve()

    # Candidate 1: cwd has firmware/data (running from repo root)
    if (Path.cwd() / "firmware").is_dir():
        return (Path.cwd() / "firmware" / "data").resolve()

    # Candidate 2: cwd is firmware directory (has platformio.ini)
    if (Path.cwd() / "platformio.ini").is_file():
        return (Path.cwd() / "data").resolve()

    # Candidate 3: data directory relative to script (firmware/data)
    script_firmware_dir = Path(__file__).resolve().parent.parent
    return (script_firmware_dir / "data").resolve()


def clean_and_prepare_target(target_dir: Path) -> None:
    """Ensure target directory exists and is clean of prior contents."""
    if target_dir.exists():
        if target_dir.is_file():
            target_dir.unlink()
            target_dir.mkdir(parents=True, exist_ok=True)
        else:
            for item in target_dir.iterdir():
                if item.is_dir():
                    shutil.rmtree(item)
                else:
                    item.unlink()
    else:
        target_dir.mkdir(parents=True, exist_ok=True)


def compute_total_size(target_dir: Path) -> int:
    """Recursively compute total size in bytes of all files in target directory."""
    total_size = 0
    for root, _, files in os.walk(target_dir):
        for filename in files:
            file_path = Path(root) / filename
            total_size += file_path.stat().st_size
    return total_size


def sync_webapp(
    dist_dir: str | Path,
    target_dir: str | Path,
    budget: int = MAX_BUDGET_BYTES,
) -> int:
    """Sync assets from dist_dir into target_dir with gzip compression and budget check.

    Returns the total compressed size in bytes.
    Raises SourceDirNotFoundError if dist_dir does not exist or is not a directory.
    Raises BudgetExceededError if total size >= budget.
    """
    dist_path = Path(dist_dir).resolve()
    target_path = Path(target_dir).resolve()

    if not dist_path.exists():
        raise SourceDirNotFoundError(f"Source distribution directory does not exist: {dist_path}")
    if not dist_path.is_dir():
        raise SourceDirNotFoundError(f"Source distribution path is not a directory: {dist_path}")

    clean_and_prepare_target(target_path)

    for root, _, files in os.walk(dist_path):
        for filename in sorted(files):
            src_file = Path(root) / filename
            rel_path = src_file.relative_to(dist_path)

            if filename.endswith(".gz"):
                dest_file = target_path / rel_path
                dest_file.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src_file, dest_file)
            else:
                dest_file = target_path / f"{rel_path}.gz"
                dest_file.parent.mkdir(parents=True, exist_ok=True)
                with open(src_file, "rb") as f_in:
                    data = f_in.read()
                compressed = gzip.compress(data, compresslevel=9)
                with open(dest_file, "wb") as f_out:
                    f_out.write(compressed)

    total_size = compute_total_size(target_path)

    if total_size >= budget:
        raise BudgetExceededError(total_size, budget)

    return total_size


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Sync web app distribution assets to firmware LittleFS data directory as gzipped files."
    )
    parser.add_argument(
        "--dist",
        type=str,
        default=None,
        help="Path to source distribution directory (default: ../controller-webapp/dist relative to cwd or script)",
    )
    parser.add_argument(
        "--target",
        type=str,
        default=None,
        help="Path to target directory (default: firmware/data or data relative to script)",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point for asset sync."""
    args = parse_args(argv)
    dist_path = resolve_dist_path(args.dist)
    target_path = resolve_target_path(args.target)

    try:
        total_size = sync_webapp(dist_path, target_path, budget=MAX_BUDGET_BYTES)
        print(f"Total webapp asset size: {total_size} bytes (budget: {MAX_BUDGET_BYTES} bytes)")
        return 0
    except SourceDirNotFoundError as err:
        print(f"Error: {err}", file=sys.stderr)
        return 1
    except BudgetExceededError as err:
        print(f"Error: {err}", file=sys.stderr)
        return 1
    except Exception as err:
        print(f"Error: Unexpected failure during asset sync: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
