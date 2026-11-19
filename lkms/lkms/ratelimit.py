"""A small in-memory rate limiter (sliding window).

Good for one server process. With several processes or servers, put the same limits in your reverse proxy
or move this to Redis; the interface (allow / penalize) stays the same.
"""
from __future__ import annotations

import threading
import time
from collections import defaultdict, deque


class RateLimiter:
    def __init__(self) -> None:
        self._hits: dict[str, deque[float]] = defaultdict(deque)
        self._lock = threading.Lock()

    def allow(self, bucket: str, limit: int, window_s: float, cost: int = 1) -> bool:
        """True and counted when the bucket is under its limit; False when it is full."""
        now = time.monotonic()
        with self._lock:
            q = self._hits[bucket]
            while q and now - q[0] > window_s:
                q.popleft()
            if len(q) + cost > limit:
                return False
            q.extend([now] * cost)
            if len(self._hits) > 200_000:            # never grow without bound under a flood of new addresses
                for k in [k for k, v in self._hits.items() if not v or now - v[-1] > 3600]:
                    del self._hits[k]
            return True

    def count(self, bucket: str, window_s: float) -> int:
        now = time.monotonic()
        with self._lock:
            q = self._hits.get(bucket)
            if not q:
                return 0
            while q and now - q[0] > window_s:
                q.popleft()
            return len(q)

    def reset(self) -> None:
        with self._lock:
            self._hits.clear()
