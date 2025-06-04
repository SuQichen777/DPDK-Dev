#!/bin/bash

# === paramaters ===
HUGEPAGE_SIZE_MB=2048         # Hugepagesize
HUGEPAGE_COUNT=1024           # Hugepage amount
HUGEMOUNT_DIR=/mnt/huge
EXAMPLE=/home/sqc/dpdk-23.11/build/examples/dpdk-helloworld
FILE_PREFIX=mydpdk

# === Step 1: Hugepages ===
echo "[+] Setting hugepages..."
echo $HUGEPAGE_COUNT | tee /proc/sys/vm/nr_hugepages

# === Step 2: mount ===
echo "[+] Mounting hugetlbfs..."
mkdir -p $HUGEMOUNT_DIR
mount -t hugetlbfs nodev $HUGEMOUNT_DIR

# === Step 3: priority ===
echo "[+] Fixing permissions..."
chown -R $USER:$USER $HUGEMOUNT_DIR

# === Step 4: check Hugepages status ===
echo "[+] Hugepage status:"
grep Huge /proc/meminfo

# === Step 5: run DPDK example ===
echo "[+] Running DPDK application..."
$EXAMPLE --file-prefix=$FILE_PREFIX --huge-dir=$HUGEMOUNT_DIR