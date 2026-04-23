"""Extract display hints and body text from tool JSON payloads."""

import json

HINT_FIELDS = {
    "Bash": "command",
    "Edit": "file_path",
    "MultiEdit": "file_path",
    "Write": "file_path",
    "Read": "file_path",
    "NotebookEdit": "notebook_path",
    "WebFetch": "url",
    "WebSearch": "query",
    "Glob": "pattern",
    "Grep": "pattern",
}


def hint_from_tool(tool: str, tin: dict) -> str:
    field = HINT_FIELDS.get(tool)
    if field and isinstance((tin or {}).get(field), str):
        return tin[field]
    for v in (tin or {}).values():
        if isinstance(v, str):
            return v
    return json.dumps(tin or {})[:60]


def body_from_tool(tool: str, tin: dict) -> str:
    tin = tin or {}

    if tool == "AskUserQuestion":
        qs = tin.get("questions")
        if isinstance(qs, list) and qs and isinstance(qs[0], dict):
            q = qs[0].get("question") or qs[0].get("header") or ""
        else:
            q = tin.get("question", "")
        return (q or "").strip()[:500]

    if tool == "Bash":
        cmd = tin.get("command", "")
        desc = tin.get("description", "")
        return (f"{desc}\n\n$ {cmd}" if desc else f"$ {cmd}")[:500]

    if tool in ("Edit", "MultiEdit"):
        path = tin.get("file_path", "")
        oldv = str(tin.get("old_string", ""))[:180]
        newv = str(tin.get("new_string", ""))[:180]
        return f"{path}\n\n--- old\n{oldv}\n\n+++ new\n{newv}"

    if tool == "Write":
        path = tin.get("file_path", "")
        content = str(tin.get("content", ""))
        head = content[:320]
        return f"{path}\n\n{head}{('...' if len(content) > 320 else '')}"

    if tool == "Read":
        return tin.get("file_path", "")

    if tool == "WebFetch":
        url = tin.get("url", "")
        prompt = str(tin.get("prompt", ""))[:200]
        return f"{url}\n\n{prompt}" if prompt else url

    if tool == "WebSearch":
        return str(tin.get("query", ""))[:300]

    if tool in ("Glob", "Grep"):
        parts = [f"pattern: {tin.get('pattern', '')}"]
        if tin.get("path"):
            parts.append(f"path: {tin['path']}")
        if tin.get("type"):
            parts.append(f"type: {tin['type']}")
        return "\n".join(parts)[:300]

    try:
        return json.dumps(tin, indent=2)[:500]
    except Exception:
        return str(tin)[:500]
