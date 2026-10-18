#!/usr/bin/env python3
"""Link .agents/skills to .claude/skills for Claude Code.

The single source of truth for skills is .agents/skills/ (Agent Skills format).
Claude Code only loads skills from .claude/skills/, so run this once after clone:

    python tools/sync_skills.py

On Windows this creates a directory junction (no admin/Developer Mode needed).
On Unix it creates a relative symlink. .claude/skills is gitignored.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / ".agents" / "skills"
LINK = ROOT / ".claude" / "skills"


def is_link_to_source() -> bool:
    try:
        return os.path.samefile(LINK, SOURCE)
    except OSError:
        return False


def remove_existing() -> None:
    if LINK.is_symlink():
        LINK.unlink()
        return
    if os.name == "nt" and LINK.is_dir():
        try:
            os.rmdir(LINK)  # junction: removes the link only
            return
        except OSError:
            shutil.rmtree(LINK)
            return
    if LINK.is_dir():
        shutil.rmtree(LINK)
    else:
        LINK.unlink()


def create_link() -> None:
    LINK.parent.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(LINK), str(SOURCE)],
            check=True,
        )
    else:
        LINK.symlink_to(
            os.path.relpath(SOURCE, LINK.parent),
            target_is_directory=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Link .agents/skills to .claude/skills for Claude Code."
    )
    parser.add_argument(
        "--replace",
        action="store_true",
        help="remove an existing .claude/skills (real directory or wrong link) and recreate it",
    )
    args = parser.parse_args()

    if not SOURCE.is_dir():
        print(f"error: skill source not found: {SOURCE}", file=sys.stderr)
        return 1

    if os.path.lexists(LINK):
        if is_link_to_source():
            print(f"already linked: {LINK} -> {SOURCE}")
            return 0
        if not args.replace:
            print(
                f"error: {LINK} already exists and is not a link to {SOURCE}.\n"
                f"Rerun with --replace to remove it.",
                file=sys.stderr,
            )
            return 1
        print(f"removing existing path: {LINK}")
        remove_existing()

    create_link()
    print(f"created: {LINK} -> {SOURCE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
