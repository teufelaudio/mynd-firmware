#!/usr/bin/env python3
"""
Async subprocess helpers for the RPi daemon.
"""

from __future__ import annotations

import asyncio
import subprocess
from dataclasses import dataclass
from typing import Sequence


@dataclass(slots=True)
class CommandResult:
    args: tuple[str, ...]
    returncode: int
    stdout: str
    stderr: str


class AsyncCommandRunner:
    """Run subprocesses without blocking the asyncio event loop."""

    async def run(self, command: Sequence[str], timeout: float = 5.0) -> CommandResult:
        process = await asyncio.create_subprocess_exec(
            *command,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        try:
            stdout_bytes, stderr_bytes = await asyncio.wait_for(process.communicate(), timeout)
        except asyncio.TimeoutError as exc:
            process.kill()
            stdout_bytes, stderr_bytes = await process.communicate()
            raise subprocess.TimeoutExpired(
                cmd=list(command),
                timeout=timeout,
                output=stdout_bytes.decode("utf-8", errors="replace"),
                stderr=stderr_bytes.decode("utf-8", errors="replace"),
            ) from exc

        return CommandResult(
            args=tuple(command),
            returncode=process.returncode,
            stdout=stdout_bytes.decode("utf-8", errors="replace"),
            stderr=stderr_bytes.decode("utf-8", errors="replace"),
        )


def with_optional_sudo(command: Sequence[str], use_sudo: bool = False) -> list[str]:
    final_command = list(command)
    if use_sudo:
        final_command.insert(0, "sudo")
    return final_command
