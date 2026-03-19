#!/usr/bin/env python3
"""
ActionsLink Daemon for PC

Main daemon that communicates with MCU using ActionsLink protocol
and integrates with Moode audio system via REST API.
"""

import argparse
import asyncio
import configparser
import logging
import logging.handlers
import os
import sys
import signal
from pathlib import Path
from typing import Optional, Dict, Any
from urllib.parse import quote

try:
    import aiohttp
except ImportError:
    print("ERROR: aiohttp is required. Install with: pip3 install aiohttp", file=sys.stderr)
    sys.exit(1)

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from actionslink_hdlc import HdlcFraming
from actionslink_transport import ActionsLinkTransport
from actionslink_api import ActionsLinkAPI

logger = logging.getLogger("ActionsLinkDaemon")


class MoodeClient:
    """Async client for Moode REST API"""
    
    def __init__(self, base_url: str, timeout: float = 5.0, retry_count: int = 3):
        self.base_url = base_url.rstrip('/')
        self.timeout = timeout
        self.retry_count = retry_count
        self.session: Optional[aiohttp.ClientSession] = None
    
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
                            text = await response.text()
                            if not self._check_error(text):
                                return {'status': 'ok', 'text': text}
                            return None
                    else:
                        if attempt < self.retry_count - 1:
                            await asyncio.sleep(0.5)
                        else:
                            logger.warning(f"Moode API {method} {endpoint}: HTTP {response.status}")
            except Exception as e:
                if attempt < self.retry_count - 1:
                    await asyncio.sleep(0.5)
                else:
                    logger.error(f"Moode API {method} {endpoint} failed: {e}")
        
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
    
    async def volume_up(self):
        """Increase volume"""
        cmd = quote('set_volume -up 5')
        return await self._api_call('GET', f'/command/?cmd={cmd}')
    
    async def volume_down(self):
        """Decrease volume"""
        cmd = quote('set_volume -dn 5')
        return await self._api_call('GET', f'/command/?cmd={cmd}')
    
    async def toggle_playback(self):
        """Toggle play/pause"""
        cmd = quote('toggle_play_pause')
        return await self._api_call('GET', f'/command/?cmd={cmd}')
    
    async def next_station(self):
        """Next radio station"""
        cmd = quote('next')
        return await self._api_call('GET', f'/command/?cmd={cmd}')
    
    async def previous_station(self):
        """Previous radio station"""
        cmd = quote('previous')
        return await self._api_call('GET', f'/command/?cmd={cmd}')
    
    async def get_volume(self) -> Optional[int]:
        """Get current volume"""
        cmd = quote('get_volume')
        result = await self._api_call('GET', f'/command/?cmd={cmd}')
        if result and 'volume' in result:
            try:
                return int(result['volume'])
            except (ValueError, TypeError):
                pass
        return None
    
    async def set_volume(self, volume: int) -> bool:
        """Set volume"""
        cmd = quote(f'set_volume {volume}')
        result = await self._api_call('GET', f'/command/?cmd={cmd}')
        return result is not None


class ActionsLinkDaemon:
    """Main daemon class"""
    
    def __init__(self, config_path: str):
        self.config = self._load_config(config_path)
        self.running = False
        self.hdlc: Optional[HdlcFraming] = None
        self.transport: Optional[ActionsLinkTransport] = None
        self.api: Optional[ActionsLinkAPI] = None
        self.moode: Optional[MoodeClient] = None
        self.logger = self._setup_logging()
        self._shutdown_scheduled = False
    
    def _load_config(self, config_path: str) -> Dict[str, Any]:
        """Load configuration from file"""
        config = configparser.ConfigParser()
        config.read(config_path)
        
        defaults = {
            'uart': {
                'device': '/dev/serial0',
                'baudrate': '115200',
            },
            'moode': {
                'base_url': 'http://localhost',
                'api_timeout': '5.0',
                'retry_count': '3'
            },
            'logging': {
                'level': 'INFO',
                'use_syslog': 'false'
            }
        }
        
        result = {}
        for section, values in defaults.items():
            result[section] = {}
            if config.has_section(section):
                for key, default_value in values.items():
                    result[section][key] = config.get(section, key, fallback=default_value)
            else:
                result[section] = values.copy()
        
        return result
    
    def _setup_logging(self) -> logging.Logger:
        """Setup logging"""
        logger_obj = logging.getLogger('actionslink_daemon')
        
        use_syslog = self.config['logging'].get('use_syslog', 'false').lower() == 'true'
        level_str = self.config['logging'].get('level', 'INFO').upper()
        level = getattr(logging, level_str, logging.INFO)
        
        logger_obj.setLevel(level)
        
        formatter = logging.Formatter('%(asctime)s [%(levelname)s] %(message)s')
        
        if use_syslog:
            try:
                handler = logging.handlers.SysLogHandler(address='/dev/log')
                handler.setFormatter(logging.Formatter('actionslink_daemon[%(process)d]: %(message)s'))
            except Exception:
                handler = logging.StreamHandler(sys.stdout)
                handler.setFormatter(formatter)
        else:
            handler = logging.StreamHandler(sys.stdout)
            handler.setFormatter(formatter)
        
        logger_obj.addHandler(handler)
        return logger_obj
    
    def handle_event(self, event):
        """Handle events from MCU"""
        event_type = event.WhichOneof("Event")
        self.logger.info(f"Event received: {event_type}")
        
        # Handle specific events
        if event_type == 'notify_power_state':
            asyncio.create_task(self._handle_power_state(event.notify_power_state))
        elif event_type == 'notify_volume':
            asyncio.create_task(self._handle_volume(event.notify_volume))
        elif event_type == 'notify_bt_avrcp_state':
            asyncio.create_task(self._handle_avrcp_state(event.notify_bt_avrcp_state))
    
    async def _handle_power_state(self, power_state):
        """Handle power state change"""
        self.logger.info(f"Power state: {power_state}")
        # Implement power state handling
    
    async def _handle_volume(self, volume):
        """Handle volume change"""
        self.logger.debug(f"Volume: {volume}")
        # Implement volume handling
    
    async def _handle_avrcp_state(self, avrcp_state):
        """Handle AVRCP state change"""
        self.logger.debug(f"AVRCP state: {avrcp_state}")
        # Implement AVRCP handling
    
    async def run(self):
        """Main daemon loop"""
        self.logger.info("Starting ActionsLink Daemon")
        
        device = self.config['uart']['device']
        
        self.moode = MoodeClient(
            self.config['moode']['base_url'],
            float(self.config['moode']['api_timeout']),
            int(self.config['moode']['retry_count'])
        )
        await self.moode.start()
        
        self.hdlc = HdlcFraming(device, self)
        if not await self.hdlc.open():
            self.logger.error("Failed to open UART")
            return 1
        
        self.transport = ActionsLinkTransport(self)
        self.transport.set_hdlc(self.hdlc)
        
        self.api = ActionsLinkAPI(self.transport, self)
        await self.api.start()
        
        self.hdlc.start()
        
        if not await self.transport.perform_handshake():
            self.logger.warning("Continuing without handshake acknowledgement")
        
        await asyncio.sleep(0.2)
        
        self.running = True
        
        try:
            while self.running:
                await asyncio.sleep(1)
        except KeyboardInterrupt:
            self.logger.info("Received interrupt signal")
        finally:
            self.running = False
            await self.api.stop()
            await self.hdlc.stop()
            await self.hdlc.close()
            await self.moode.stop()
            self.logger.info("ActionsLink Daemon stopped")
        
        return 0
    
    def stop(self):
        """Stop the daemon"""
        self.running = False


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description='ActionsLink Daemon')
    parser.add_argument('--config', default='/etc/actionslink_daemon.conf',
                       help='Path to configuration file')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.config):
        print(f"ERROR: Configuration file not found: {args.config}", file=sys.stderr)
        sys.exit(1)
    
    daemon = ActionsLinkDaemon(args.config)
    
    def signal_handler(sig, frame):
        daemon.stop()
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    return asyncio.run(daemon.run())


if __name__ == '__main__':
    sys.exit(main())