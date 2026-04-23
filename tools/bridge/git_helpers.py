"""Git project introspection: branch, project name, dirty count."""

import os
import subprocess
import time

GIT_TTL_SEC = 10


def _git(cwd, *args, timeout=2.0):
    try:
        out = subprocess.run(
            ("git", *args),
            cwd=cwd,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        return out.stdout.strip() if out.returncode == 0 else ""
    except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
        return ""


def refresh_git(state, sid: str, cwd: str):
    if not cwd or not os.path.isdir(cwd):
        return
    now = time.time()
    meta = state.session_meta.get(sid) or {}
    if meta.get("cwd") == cwd and (now - meta.get("checked_at", 0)) < GIT_TTL_SEC:
        return
    root = _git(cwd, "rev-parse", "--show-toplevel") or cwd
    state.session_meta[sid] = {
        "cwd": cwd,
        "project": os.path.basename(root.rstrip("/"))[:39] or "",
        "branch": _git(cwd, "rev-parse", "--abbrev-ref", "HEAD")[:39],
        "dirty": sum(
            1 for ln in _git(cwd, "status", "--porcelain").splitlines() if ln.strip()
        ),
        "checked_at": now,
    }
