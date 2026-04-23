"""Parse Claude Code JSONL transcript files for model, context, and reply."""

import json
import os
import re
import sys
from datetime import datetime


def log(*a, **kw):
    ts = datetime.now().isoformat(timespec="milliseconds")
    print(f"[{ts}]", *a, file=sys.stderr, flush=True, **kw)


def short_model(full: str) -> str:
    if not full:
        return ""
    s = full.lower()
    family = "Claude"
    for tag, label in (("opus", "Opus"), ("sonnet", "Sonnet"), ("haiku", "Haiku")):
        if tag in s:
            family = label
            break
    m = re.search(r"(\d+)[\.\-](\d+)", s)
    if m:
        return f"{family} {m.group(1)}.{m.group(2)}"
    return family if family != "Claude" else full[:28]


def extract_session_context(path: str) -> int:
    """Return the session's CURRENT context-window usage, approximated
    as (last assistant turn's input_tokens + output_tokens)."""
    if not path or not os.path.exists(path):
        return 0
    try:
        sz = os.path.getsize(path)
        with open(path, "rb") as f:
            f.seek(max(0, sz - 131072))
            data = f.read()
        lines = data.decode("utf-8", errors="replace").splitlines()
        for line in reversed(lines):
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            msg = obj.get("message", obj)
            if not isinstance(msg, dict) or msg.get("role") != "assistant":
                continue
            usage = msg.get("usage")
            if isinstance(usage, dict):
                inp = int(usage.get("input_tokens", 0) or 0)
                out = int(usage.get("output_tokens", 0) or 0)
                return inp + out
    except Exception:
        pass
    return 0


def extract_session_model(path: str) -> str:
    """Find the most recent assistant message in the transcript and
    return its `model` field."""
    if not path or not os.path.exists(path):
        return ""
    try:
        sz = os.path.getsize(path)
        with open(path, "rb") as f:
            f.seek(max(0, sz - 131072))
            data = f.read()
        lines = data.decode("utf-8", errors="replace").splitlines()
        for line in reversed(lines):
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            msg = obj.get("message", obj)
            if not isinstance(msg, dict) or msg.get("role") != "assistant":
                continue
            m = msg.get("model")
            if isinstance(m, str) and m:
                return m
    except Exception:
        pass
    return ""


def extract_last_assistant(path: str) -> str:
    if not path or not os.path.exists(path):
        return ""
    try:
        sz = os.path.getsize(path)
        with open(path, "rb") as f:
            f.seek(max(0, sz - 131072))
            data = f.read()
        lines = data.decode("utf-8", errors="replace").splitlines()
        for line in reversed(lines):
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            msg = obj.get("message", obj)
            if not isinstance(msg, dict):
                continue
            if msg.get("role") != "assistant":
                continue
            content = msg.get("content")
            text = ""
            if isinstance(content, str):
                text = content
            elif isinstance(content, list):
                for block in content:
                    if isinstance(block, dict) and block.get("type") == "text":
                        text = block.get("text", "")
                        if text:
                            break
            text = (text or "").strip()
            if text:
                return " ".join(text.split())[:220]
    except Exception as e:
        log(f"[transcript] error: {e}")
    return ""
