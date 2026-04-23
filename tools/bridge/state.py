"""Central daemon state. All shared variables live here as DaemonState attributes."""

import threading
import sys
from collections import deque
from datetime import datetime


class DaemonState:
    def __init__(self, budget_limit: int = 0):
        self.lock = threading.Lock()
        self.bump = threading.Event()

        # Session tracking
        self.sessions_running: set = set()
        self.sessions_total: set = set()
        self.sessions_waiting: set = set()
        self.session_meta: dict = {}       # sid -> {cwd, project, branch, dirty, checked_at}
        self.session_context: dict = {}    # sid -> int (token count)
        self.session_model: dict = {}      # sid -> str
        self.session_assistant: dict = {}  # sid -> str

        # Prompt / approval queue
        self.active_prompt: dict | None = None
        self.pending_prompts: dict = {}    # prompt_id -> prompt dict
        self.pending: dict = {}            # prompt_id -> {"event", "decision"}

        # Global display state
        self.focused_sid: str | None = None
        self.transcript: deque = deque(maxlen=8)
        self.budget_limit: int = budget_limit
        self.model_name: str = ""
        self.assistant_msg: str = ""

        # Transport reference (set once at startup)
        self.transport = None

    def add_transcript(self, line: str):
        with self.lock:
            self.transcript.appendleft(f"{now_hm()} {line[:80]}")


def log(*a, **kw):
    ts = datetime.now().isoformat(timespec="milliseconds")
    print(f"[{ts}]", *a, file=sys.stderr, flush=True, **kw)


def now_hm():
    return datetime.now().strftime("%H:%M")
