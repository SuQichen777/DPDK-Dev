#!/bin/bash
set -e

echo "[+] Running hugepage setup..."
/users/Jiaxi/DPDK-Dev/script/get-hugepage-available.sh

echo "[+] Running DPDK NIC binding..."
/users/Jiaxi/DPDK-Dev/script/bind-NIC.sh

echo "[+] DPDK initialization complete."

