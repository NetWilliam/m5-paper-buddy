"""HTTP hook handler for Claude Code events."""

import json
import os
import threading
import time

from bridge.state import log
from bridge.git_helpers import refresh_git
from bridge.tool_display import body_from_tool, hint_from_tool
from bridge.transcript import (
    extract_last_assistant,
    extract_session_context,
    extract_session_model,
    short_model,
)
from http.server import BaseHTTPRequestHandler


class HookHandler(BaseHTTPRequestHandler):
    state = None
    protocol = None

    def log_message(self, fmt, *args):
        pass

    def do_POST(self):
        try:
            n = int(self.headers.get("Content-Length") or "0")
            body = self.rfile.read(n) if n > 0 else b""
            payload = json.loads(body.decode("utf-8")) if body else {}
        except Exception as e:
            return self._reply(400, {"error": str(e)})

        event = payload.get("hook_event_name", "")
        log(f"[hook] {event} session={payload.get('session_id', '')[:8]}")

        sid = payload.get("session_id", "")
        cwd = payload.get("cwd", "")
        if sid and cwd:
            refresh_git(self.state, sid, cwd)

        for k in ("model", "model_id", "assistant_model"):
            v = payload.get(k)
            if isinstance(v, str) and v:
                self.state.model_name = short_model(v)
                break

        tp = payload.get("transcript_path")
        if isinstance(tp, str) and tp:
            if sid:
                m = extract_session_model(tp)
                if m:
                    self.state.session_model[sid] = short_model(m)
            latest = extract_last_assistant(tp)
            if latest:
                if sid and self.state.session_assistant.get(sid) != latest:
                    self.state.session_assistant[sid] = latest
                    self.state.bump.set()
                if latest != self.state.assistant_msg:
                    self.state.assistant_msg = latest
                    self.state.bump.set()
            if sid:
                ctx = extract_session_context(tp)
                if self.state.session_context.get(sid) != ctx:
                    self.state.session_context[sid] = ctx
                    self.state.bump.set()

        try:
            if event == "SessionStart":
                resp = self._session_start(payload)
            elif event == "Stop":
                resp = self._session_stop(payload)
            elif event == "UserPromptSubmit":
                resp = self._user_prompt(payload)
            elif event == "PreToolUse":
                resp = self._pretool(payload)
            elif event == "PostToolUse":
                resp = self._posttool(payload)
            else:
                resp = {}
        except Exception as e:
            log(f"[hook] handler error: {e!r}")
            resp = {}

        self._reply(200, resp)

    def _reply(self, code: int, obj: dict):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except BrokenPipeError:
            pass

    def _session_start(self, p):
        sid = p.get("session_id", "")
        with self.state.lock:
            self.state.sessions_total.add(sid)
            self.state.sessions_running.add(sid)
        proj = (self.state.session_meta.get(sid) or {}).get("project", "")
        self.state.add_transcript(f"session: {proj}" if proj else "session started")
        self.state.bump.set()
        return {}

    def _session_stop(self, p):
        sid = p.get("session_id", "")
        with self.state.lock:
            self.state.sessions_running.discard(sid)
        self.state.add_transcript("session done")
        self.state.bump.set()
        return {}

    def _user_prompt(self, p):
        prompt = (p.get("prompt") or "").strip().replace("\n", " ")
        if prompt:
            self.state.add_transcript(f"> {prompt[:60]}")
            self.state.bump.set()
        return {}

    def _posttool(self, p):
        tool = p.get("tool_name", "?")
        self.state.add_transcript(f"{tool} done")
        self.state.bump.set()
        return {}

    def _pretool(self, p):
        state = self.state
        sid = p.get("session_id", "")
        tool = p.get("tool_name", "?")
        tin = p.get("tool_input") or {}

        if p.get("permission_mode") == "bypassPermissions":
            state.add_transcript(f"{tool} (bypass)")
            state.bump.set()
            return {
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "allow",
                    "permissionDecisionReason": "bypass-permissions mode",
                }
            }

        hint = hint_from_tool(tool, tin)
        body = body_from_tool(tool, tin)

        kind = "question" if tool == "AskUserQuestion" else "permission"
        option_labels = []
        if kind == "question":
            qs = tin.get("questions")
            if isinstance(qs, list) and qs and isinstance(qs[0], dict):
                for o in (qs[0].get("options") or [])[:4]:
                    option_labels.append(
                        str(o.get("label")) if isinstance(o, dict) else str(o)
                    )
            else:
                for o in (tin.get("options") or [])[:4]:
                    option_labels.append(
                        str(o.get("label")) if isinstance(o, dict) else str(o)
                    )

        prompt_id = f"req_{int(time.time() * 1000)}_{os.getpid()}"
        event = threading.Event()
        holder = {"event": event, "decision": None}
        state.pending[prompt_id] = holder

        prompt_obj = {
            "id": prompt_id,
            "tool": tool,
            "hint": hint,
            "body": body,
            "kind": kind,
            "option_labels": option_labels,
            "session_id": sid,
        }

        with state.lock:
            state.sessions_waiting.add(sid)
            state.pending_prompts[prompt_id] = prompt_obj
            if state.active_prompt is None:
                state.active_prompt = prompt_obj
        state.bump.set()

        try:
            got = event.wait(timeout=30)
            decision = holder["decision"] if got else None
            if isinstance(decision, str) and decision.startswith("option:"):
                time.sleep(0.6)
        finally:
            state.pending.pop(prompt_id, None)
            with state.lock:
                state.sessions_waiting.discard(sid)
                state.pending_prompts.pop(prompt_id, None)
                if state.active_prompt and state.active_prompt["id"] == prompt_id:
                    state.active_prompt = next(iter(state.pending_prompts.values()), None)
            state.bump.set()

        if isinstance(decision, str) and decision.startswith("option:"):
            try:
                idx = int(decision.split(":", 1)[1])
            except ValueError:
                idx = -1
            label = option_labels[idx] if 0 <= idx < len(option_labels) else ""
            state.add_transcript(f"{tool} → {label[:30]}")
            state.bump.set()
            return {
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "deny",
                    "permissionDecisionReason": (
                        f"The user answered on the M5Paper buddy device: "
                        f"'{label}' (option {idx + 1}). Proceed using this answer "
                        f"directly — do NOT call AskUserQuestion again."
                    ),
                }
            }

        if decision == "once":
            state.add_transcript(f"{tool} allow")
            state.bump.set()
            return {
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "allow",
                    "permissionDecisionReason": "Approved on M5Paper",
                }
            }
        if decision == "deny":
            state.add_transcript(f"{tool} deny")
            state.bump.set()
            reason = (
                (
                    "The user cancelled this question on the M5Paper "
                    "buddy without answering. Ask them directly in the "
                    "terminal instead."
                )
                if kind == "question"
                else "Denied on M5Paper"
            )
            return {
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "deny",
                    "permissionDecisionReason": reason,
                }
            }
        state.add_transcript(f"{tool} timeout")
        state.bump.set()
        return {}
