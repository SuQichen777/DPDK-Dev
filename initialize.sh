#!/bin/bash
set -e

echo "[+] Running hugepage setup..."
/home/sqc/script/get-hugepage-available.sh

echo "[+] Running DPDK NIC binding..."
/home/sqc/script/bind-NIC.sh

echo "[+] DPDK initialization complete."

#chmod +x ~/initialize.sh
#sudo nano /etc/systemd/system/dpdk-bind.service

# [Unit]
# Description=Run My DPDK Init Script
# After=network.target

# [Service]
# Type=oneshot
# ExecStart=/bin/bash /home/yourusername/initialize.sh
# RemainAfterExit=true

# [Install]
# WantedBy=multi-user.target

# systemctl status dpdk-bind.service
