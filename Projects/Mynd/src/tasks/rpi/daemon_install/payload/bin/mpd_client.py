#!/usr/bin/env python3
"""
MPD state queries for both one-shot checks and monitor polling.
"""

from __future__ import annotations

import asyncio
import socket
import threading
import time
from typing import Optional


class MpdClient:
    def __init__(self, logger, port: int):
        self.logger = logger
        self.port = port
        self._persistent_sock: Optional[socket.socket] = None
        self._lock = threading.Lock()
        self._last_fail_log_time = 0.0
        self._fail_log_interval = 60.0

    async def query_state(self) -> Optional[str]:
        return await asyncio.to_thread(self._query_state_once)

    async def query_state_for_monitor(self) -> Optional[str]:
        return await asyncio.to_thread(self._query_state_persistent)

    def close(self):
        with self._lock:
            if self._persistent_sock is None:
                return
            try:
                self._persistent_sock.close()
            except Exception:
                pass
            self._persistent_sock = None
            self.logger.info("Closed persistent MPD socket")

    def _query_state_once(self) -> Optional[str]:
        result = self._try_connection("/run/mpd/socket", is_unix_socket=True)
        if result is not None:
            return result
        return self._try_connection(("localhost", self.port), is_unix_socket=False)

    def _query_state_persistent(self) -> Optional[str]:
        with self._lock:
            if self._persistent_sock is None:
                self._connect_persistent()
                if self._persistent_sock is None:
                    return None

            try:
                self._persistent_sock.sendall(b"status\n")
                return self._read_state(self._persistent_sock)
            except Exception as exc:
                self._rate_limited_error("Error querying MPD via persistent socket: %s", exc)
                try:
                    self._persistent_sock.close()
                except Exception:
                    pass
                self._persistent_sock = None
                return None

    def _try_connection(self, address, is_unix_socket: bool) -> Optional[str]:
        try:
            if is_unix_socket:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.settimeout(2.0)
                sock.connect(address)
            else:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(2.0)
                host, port = address
                sock.connect((host, port))

            result = self._query_on_socket(sock)
            sock.close()
            return result
        except (FileNotFoundError, socket.timeout, socket.error, Exception):
            return None

    def _connect_persistent(self):
        for address, is_unix_socket, label in (
            ("/run/mpd/socket", True, "Unix socket"),
            (("localhost", self.port), False, "TCP"),
        ):
            try:
                if is_unix_socket:
                    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    sock.settimeout(2.0)
                    sock.connect(address)
                else:
                    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    sock.settimeout(2.0)
                    host, port = address
                    sock.connect((host, port))

                greeting = sock.recv(1024).decode("utf-8")
                if greeting.startswith("OK MPD"):
                    self._persistent_sock = sock
                    self.logger.info("Connected to MPD via %s", label)
                    return
                sock.close()
            except (FileNotFoundError, socket.timeout, socket.error, Exception) as exc:
                self._rate_limited_error("MPD %s connection failed: %s", label, exc)

        self._persistent_sock = None

    def _query_on_socket(self, sock: socket.socket) -> Optional[str]:
        try:
            greeting = sock.recv(1024).decode("utf-8")
            if not greeting.startswith("OK MPD"):
                self.logger.debug("Unexpected MPD greeting: %s", greeting[:50])
                return None

            sock.sendall(b"status\n")
            return self._read_state(sock)
        except Exception as exc:
            self.logger.error("Error reading MPD response: %s", exc)
            return None

    def _read_state(self, sock: socket.socket) -> Optional[str]:
        response = b""
        while True:
            chunk = sock.recv(1024)
            if not chunk:
                break
            response += chunk
            if b"\nOK\n" in response or response.endswith(b"\nOK\n"):
                break

        for line in response.decode("utf-8").split("\n"):
            line = line.strip()
            if line.startswith("state: "):
                state = line.split(":", 1)[1].strip()
                if state:
                    return state.lower()

        self.logger.debug("MPD status response did not contain state line")
        return None

    def _rate_limited_error(self, message: str, *args):
        if (time.time() - self._last_fail_log_time) < self._fail_log_interval:
            return
        self._last_fail_log_time = time.time()
        self.logger.error(message, *args)
