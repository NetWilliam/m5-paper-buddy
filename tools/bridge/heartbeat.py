"""Heartbeat construction and send loop."""

import time


def build_heartbeat(state) -> dict:
    with state.lock:
        msg = (
            f"approve: {state.active_prompt['tool']}"
            if state.active_prompt
            else (state.transcript[0][6:] if state.transcript else "idle")
        )
        hb = {
            "total": len(state.sessions_total),
            "running": len(state.sessions_running),
            "waiting": len(state.sessions_waiting),
            "msg": msg[:23],
            "entries": list(state.transcript),
            "tokens": 0,
            "tokens_today": 0,
        }
        if state.active_prompt:
            p = {
                "id": state.active_prompt["id"],
                "tool": state.active_prompt["tool"][:19],
                "hint": state.active_prompt["hint"][:43],
                "body": state.active_prompt["body"][:500],
                "kind": state.active_prompt.get("kind", "permission"),
            }
            opts = state.active_prompt.get("option_labels") or []
            if opts:
                p["options"] = opts[:4]
            sid = state.active_prompt.get("session_id", "")
            if sid:
                p["sid"] = sid[:8]
                meta = state.session_meta.get(sid) or {}
                p["project"] = meta.get("project", "")[:23]
            hb["prompt"] = p

        sessions_list = []
        for sid in list(state.sessions_total)[:5]:
            meta = state.session_meta.get(sid) or {}
            sessions_list.append(
                {
                    "sid": sid[:8],
                    "full": sid,
                    "proj": (meta.get("project", "") or "")[:22],
                    "branch": (meta.get("branch", "") or "")[:16],
                    "dirty": meta.get("dirty", 0),
                    "running": sid in state.sessions_running,
                    "waiting": sid in state.sessions_waiting,
                    "focused": sid == state.focused_sid,
                }
            )
        if sessions_list:
            hb["sessions"] = sessions_list
        if state.budget_limit > 0:
            hb["budget"] = state.budget_limit

        sid = None
        if state.focused_sid and state.focused_sid in state.session_meta:
            sid = state.focused_sid
        elif state.active_prompt and state.active_prompt.get("session_id"):
            sid = state.active_prompt["session_id"]
        elif state.sessions_running:
            sid = next(iter(state.sessions_running))
        elif state.session_meta:
            sid = max(state.session_meta, key=lambda s: state.session_meta[s].get("checked_at", 0))

        if sid and sid in state.session_meta:
            m = state.session_meta[sid]
            hb["project"] = m.get("project", "")
            hb["branch"] = m.get("branch", "")
            hb["dirty"] = m.get("dirty", 0)

        if sid:
            ctx = state.session_context.get(sid, 0)
            hb["tokens"] = ctx
            hb["tokens_today"] = ctx

        s_model = state.session_model.get(sid) if sid else None
        if s_model:
            hb["model"] = s_model
        elif state.model_name:
            hb["model"] = state.model_name

        a_msg = state.session_assistant.get(sid) if sid else None
        if a_msg:
            hb["assistant_msg"] = a_msg
        elif state.assistant_msg:
            hb["assistant_msg"] = state.assistant_msg
    return hb


def heartbeat_loop(state, protocol):
    """Send a heartbeat on BUMP (state change) or every 10s if idle.

    Rate-limited to one send per MIN_INTERVAL seconds so a busy second
    window firing hooks constantly doesn't flood the device. Bumps during
    the quiet window are coalesced into the next send.
    """
    MIN_INTERVAL = 1.0
    last_sent = 0.0
    while True:
        state.bump.wait(timeout=10)
        state.bump.clear()
        now = time.time()
        since = now - last_sent
        if since < MIN_INTERVAL:
            time.sleep(MIN_INTERVAL - since)
        protocol.send_line(build_heartbeat(state))
        last_sent = time.time()
