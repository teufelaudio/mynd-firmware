#!/bin/bash

# If the service is masked, unmask it first
sudo systemctl unmask mynd-rpi-link

# Stop and disable the service
sudo systemctl stop mynd-rpi-link
sudo systemctl disable mynd-rpi-link

# Stop and disable the configure-moode service
sudo systemctl stop configure-moode
sudo systemctl disable configure-moode

# Remove the files
sudo rm -f /etc/systemd/system/mynd-rpi-link.service
sudo rm -f /etc/systemd/system/configure-moode.service
sudo rm -f /usr/local/bin/mynd_rpi_link.py
sudo rm -f /usr/local/bin/actionslink_adapter.py
sudo rm -f /usr/local/bin/bluetooth_controller.py
sudo rm -f /usr/local/bin/command_runner.py
sudo rm -f /usr/local/bin/daemon_context.py
sudo rm -f /usr/local/bin/link_protocol.py
sudo rm -f /usr/local/bin/moode_client.py
sudo rm -f /usr/local/bin/mpd_client.py
sudo rm -f /usr/local/bin/playback_controller.py
sudo rm -f /usr/local/bin/power_controller.py
sudo rm -f /usr/local/bin/request_handlers.py
sudo rm -f /usr/local/bin/wifi_controller.py
sudo rm -f /usr/local/bin/configure_moode.py
sudo rm -rf /usr/local/bin/actionslink/
sudo rm -f /etc/mynd_rpi_link.conf
sudo rm -f /etc/sudoers.d/mynd-rpi-link

# Remove remote-install staging dir if present (e.g. from interrupted install)
sudo rm -rf /tmp/mynd_berry_install

# Reload systemd
sudo systemctl daemon-reload

echo "Uninstall complete."
