#!/bin/bash
set -e

# === Set Parameters ===
DPDK_DIR=/home/sqc/dpdk-23.11
DEVBIND=$DPDK_DIR/usertools/dpdk-devbind.py
VFIO_MODULE=uio_pci_generic
PCI_DEVICES=("0000:03:00.0" "0000:1a:00.0")

# === Load Module ===
echo "[+] Loading DPDK driver module: $VFIO_MODULE"
modprobe $VFIO_MODULE

# === Bind NIC ===
echo "[+] Binding NICs to $VFIO_MODULE for DPDK"
for dev in "${PCI_DEVICES[@]}"; do
  echo "    - Binding $dev"
  python3 $DEVBIND -b $VFIO_MODULE $dev
done

# === Show current status ===
echo "[+] Current NIC binding status:"
python3 $DEVBIND --status
