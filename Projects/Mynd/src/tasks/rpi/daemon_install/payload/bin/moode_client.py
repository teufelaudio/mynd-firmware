#!/usr/bin/env python3
"""
Moode REST API Client

Async client for communicating with Moode OS REST API.
Volume operations use vol.sh directly (bypasses the REST API's renderer check
that blocks set_volume when Spotify/AirPlay/BT is active).
"""

import asyncio
import logging
import sqlite3
import time
import json as json_lib
from typing import Optional, Dict
from urllib.parse import quote

from command_runner import AsyncCommandRunner

try:
    import aiohttp
except ImportError:
    print("ERROR: aiohttp is required. Install with: pip3 install aiohttp", file=__import__('sys').stderr)
    raise

logger = logging.getLogger("MoodeClient")

# Initial volume step limit and maximum volumes
# These are placeholder values and will be updated
# by refresh_volume_settings() with actual values 
# from Moode DB.
_MOODE_DEFAULT_STEP_LIMIT = 5
_MOODE_DEFAULT_MAX_VOLUME = 100
_MOODE_DB_PATH = "/var/local/www/db/moode-sqlite3.db"


class MoodeClient:
    """Async client for Moode REST API"""
    
    # Rate limit for connection/API error logs (seconds between logs)
    _api_error_log_interval = 60.0

    def __init__(
        self,
        base_url: str,
        timeout: float = 5.0,
        retry_count: int = 3,
        command_runner: Optional[AsyncCommandRunner] = None,
    ):
        self.base_url = base_url.rstrip('/')
        self.timeout = timeout
        self.retry_count = retry_count
        self.session: Optional[aiohttp.ClientSession] = None
        self._last_api_error_log_time: float = 0.0
        self._volume_step_limit: int = _MOODE_DEFAULT_STEP_LIMIT
        self._volume_mpd_max: int = _MOODE_DEFAULT_MAX_VOLUME
        self._command_runner = command_runner or AsyncCommandRunner()
    
    async def start(self):
        """Start HTTP session"""
        self.session = aiohttp.ClientSession(
            timeout=aiohttp.ClientTimeout(total=self.timeout),
            connector=aiohttp.TCPConnector(ssl=False)
        )
    
    async def stop(self):
        """Stop HTTP session"""
        if self.session:
            await self.session.close()

    async def refresh_volume_settings(self):
        """Load Moode volume step/max from DB and cache them."""
        def _load():
            step = _MOODE_DEFAULT_STEP_LIMIT
            mpd_max = _MOODE_DEFAULT_MAX_VOLUME
            try:
                with sqlite3.connect(_MOODE_DB_PATH, timeout=1.0) as conn:
                    cur = conn.cursor()
                    cur.execute(
                        "SELECT param, value FROM cfg_system "
                        "WHERE param IN ('volume_step_limit', 'volume_mpd_max')"
                    )
                    for param, value in cur.fetchall():
                        try:
                            v = int(str(value).strip())
                        except (TypeError, ValueError):
                            continue
                        if param == "volume_step_limit":
                            step = max(1, min(100, v))
                        elif param == "volume_mpd_max":
                            mpd_max = max(0, min(100, v))
            except Exception as e:
                logger.warning("Failed to load Moode volume settings from DB: %s", e)
            return step, mpd_max

        step, mpd_max = await asyncio.get_event_loop().run_in_executor(None, _load)
        self._volume_step_limit = step
        self._volume_mpd_max = mpd_max
        logger.info("Moode volume settings: step_limit=%s, mpd_max=%s", step, mpd_max)

    def get_volume_step_limit(self) -> int:
        return self._volume_step_limit

    def get_volume_mpd_max(self) -> int:
        return self._volume_mpd_max
    
    async def _api_call(self, method: str, endpoint: str, **kwargs) -> Optional[Dict]:
        """Make HTTP request to Moode API"""
        url = f"{self.base_url}{endpoint}"
        
        for attempt in range(self.retry_count):
            try:
                async with self.session.request(method, url, **kwargs) as response:
                    if response.status == 200:
                        try:
                            return await response.json()
                        except:
                            # Response is not valid JSON, try parsing as text
                            text = await response.text()
                            if not self._check_error(text):
                                # Try to parse text as JSON (Moode sometimes returns JSON strings in text field)
                                try:
                                    parsed = json_lib.loads(text)
                                    return parsed
                                except (json_lib.JSONDecodeError, ValueError):
                                    return {'status': 'ok', 'text': text}
                            return None
                    else:
                        if attempt < self.retry_count - 1:
                            await asyncio.sleep(0.5)
                        else:
                            if (time.time() - self._last_api_error_log_time) >= self._api_error_log_interval:
                                self._last_api_error_log_time = time.time()
                                logger.warning(f"Moode API {method} {endpoint}: HTTP {response.status}")
            except Exception as e:
                if attempt < self.retry_count - 1:
                    await asyncio.sleep(0.5)
                else:
                    if (time.time() - self._last_api_error_log_time) >= self._api_error_log_interval:
                        self._last_api_error_log_time = time.time()
                        err_msg = str(e) or repr(e)
                        logger.error(
                            "Moode API %s %s failed: %s: %s",
                            method, endpoint, type(e).__name__, err_msg,
                        )
        
        return None
    
    def _check_error(self, text: str) -> bool:
        """Check if response contains error"""
        try:
            if text and ('error' in text.lower() or 'ack' in text.lower()):
                logger.warning(f"Moode API error in response: {text[:200]}")
                return True
        except:
            pass
        return False

    # -----------------------------------------------------------------------
    # Volume control via vol.sh.
    # Moode's REST API set_volume command can be blocked when a renderer is active.
    # We use vol.sh directly and follow Moode's documented CLI form:
    #   vol.sh -up <step>, vol.sh -dn <step>, vol.sh <absolute_volume>
    # -----------------------------------------------------------------------

    async def volume_up(self) -> bool:
        """Increase volume via vol.sh using configured Moode step."""
        return await self._vol_sh('-up', str(self._volume_step_limit))

    async def volume_down(self) -> bool:
        """Decrease volume via vol.sh using configured Moode step."""
        return await self._vol_sh('-dn', str(self._volume_step_limit))

    async def get_volume(self) -> Optional[int]:
        """Get current volume via vol.sh (reads volknob from DB)"""
        try:
            result = await self._command_runner.run(['/var/www/util/vol.sh'], timeout=5)
            if result.returncode == 0 and result.stdout.strip().isdigit():
                return int(result.stdout.strip())
        except Exception as e:
            logger.warning("vol.sh (get) failed: %s", e)
        return None

    async def set_volume(self, volume: int) -> bool:
        """Set absolute volume via vol.sh (bypasses REST API renderer check)"""
        return await self._vol_sh(str(max(0, min(100, volume))))

    async def _vol_sh(self, *args: str) -> bool:
        """Call moode's vol.sh directly. Unlike the REST API set_volume command,
        vol.sh does not block when a renderer (Spotify/AirPlay/BT) is active."""
        try:
            result = await self._command_runner.run(['/var/www/util/vol.sh', *args], timeout=5)
            if result.returncode != 0:
                logger.warning(
                    "vol.sh %s failed (rc=%s, stdout=%r, stderr=%r)",
                    ' '.join(args), result.returncode,
                    (result.stdout or "").strip(),
                    (result.stderr or "").strip(),
                )
                return False
            return True
        except Exception as e:
            logger.warning("vol.sh %s failed: %s", ' '.join(args), e)
            return False

    # -----------------------------------------------------------------------
    # Playback control (REST API)
    # -----------------------------------------------------------------------

    async def toggle_playback(self):
        """Toggle play/pause"""
        cmd = quote('toggle_play_pause')
        return await self._api_call('GET', f'/command/?cmd={cmd}')
    
    async def pause_playback(self):
        """Pause playback"""
        cmd = quote('pause')
        return await self._api_call('GET', f'/command/?cmd={cmd}')
    
    async def next_station(self):
        """Next radio station"""
        cmd = quote('next')
        return await self._api_call('GET', f'/command/?cmd={cmd}')
    
    async def previous_station(self):
        """Previous radio station"""
        cmd = quote('previous')
        return await self._api_call('GET', f'/command/?cmd={cmd}')

    async def get_output_format(self) -> Optional[str]:
        """Used for two purposes when MPD is paused/stopped:
        1. Detect active renderer (MPD vs Airplay/Bluetooth/Spotify) when MPD is paused/stopped
        2. Detect if audio is actively streaming from any source (checks for PCM/kHz indicators)
        API: Moode setup_guide §6.1 REST API – get_output_format returns {"format": "PCM ..."} or {"format": "Not playing"}.
        Note: Skipped when MPD is playing (optimization - already know source=MPD, streaming=True).
        """
        cmd = quote('get_output_format')
        result = await self._api_call('GET', f'/command/?cmd={cmd}')
        if result and 'format' in result:
            return str(result['format']).strip()
        if result and 'text' in result:
            return str(result['text']).strip()
        return None
