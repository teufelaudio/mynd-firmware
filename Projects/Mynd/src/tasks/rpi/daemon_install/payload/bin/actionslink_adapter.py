#!/usr/bin/env python3
"""
Shared ActionsLink/protobuf imports for the RPi daemon.
"""

import os
import sys
from pathlib import Path


_THIS_FILE = Path(__file__).resolve()
_HOST_CANDIDATES = []

_env_host_dir = os.environ.get("ACTIONSLINK_HOST_DIR")
if _env_host_dir:
    _HOST_CANDIDATES.append(Path(_env_host_dir))

_HOST_CANDIDATES.extend(
    [
        _THIS_FILE.parent / "actionslink",
        _THIS_FILE.parent.parent.parent / "host",
    ]
)

if len(_THIS_FILE.parents) > 5:
    _HOST_CANDIDATES.append(
        _THIS_FILE.parents[5] / "external" / "teufel" / "libs" / "actionslink" / "host"
    )

ACTIONSLINK_HOST_DIR = None
for candidate in _HOST_CANDIDATES:
    if candidate.exists():
        ACTIONSLINK_HOST_DIR = candidate
        if str(candidate) not in sys.path:
            sys.path.insert(0, str(candidate))
        generated_dir = candidate / "generated"
        if generated_dir.exists() and str(generated_dir) not in sys.path:
            sys.path.insert(0, str(generated_dir))
        break

if ACTIONSLINK_HOST_DIR is None:
    ACTIONSLINK_HOST_DIR = _HOST_CANDIDATES[0]

try:
    from actionslink_client import ActionsLinkClient
    try:
        from generated import audio_pb2 as audio_pb
        from generated import battery_pb2 as battery_pb
        from generated import error_pb2 as error_pb
        from generated import host_pb2 as host_pb
        from generated import leds_pb2 as leds_pb
        from generated import message_pb2 as message_pb
        from generated import system_pb2 as system_pb
    except ImportError:
        import audio_pb2 as audio_pb
        import battery_pb2 as battery_pb
        import error_pb2 as error_pb
        import host_pb2 as host_pb
        import leds_pb2 as leds_pb
        import message_pb2 as message_pb
        import system_pb2 as system_pb
except ImportError as exc:
    print(f"ERROR: ActionsLink client not found: {exc}", file=sys.stderr)
    print(
        f"Please ensure ActionsLink host library is available at {ACTIONSLINK_HOST_DIR}",
        file=sys.stderr,
    )
    sys.exit(1)


__all__ = [
    "ACTIONSLINK_HOST_DIR",
    "ActionsLinkClient",
    "audio_pb",
    "battery_pb",
    "error_pb",
    "host_pb",
    "leds_pb",
    "message_pb",
    "system_pb",
]
