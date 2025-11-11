## Initial Setup

After cloning this repository, please do the following to make sure everything works. If the steps below are not permitted, please try `chmod +x` on the scripts.

### DPDK Build

I used meson and ninja to build DPDK, so please make sure you have installed them. If you are also running it on CloudLab, you may need to install some packages by running the following command:

#### meson and pyelftools

```bash
sudo apt update
sudo apt install python3-pip ninja-build
pip3 install --user meson
pip3 install --user pyelftools
```

Then

```bash
echo 'export PATH=$HOME/.local/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
```

#### NUMA

```bash
sudo apt update
sudo apt install libnuma-dev
```

### Raft

I wrote a simple Raft implementation in C, which is located in the `dpdk-23.11/app/raft` directory. If you would like to build it, you need to run the following install commands.

```bash
sudo apt update
sudo apt install libjansson-dev pkg-config libssl-dev libpcap-dev zlib1g-dev libarchive-dev libbsd-dev ibverbs-providers rdma-core librdmacm-dev libibverbs-dev
```

Please make sure you have `config.json` where you run the Raft application. There is already one under `dpdk-23.11`, so you can cd to this path, change corresponding values in the `config.json` file, and run the Raft application using `sudo ./build/app/dpdk-raft -l 0` (lcore used can be changed). More specific information about Raft app could be found [under the raft directory](https://github.com/SuQichen777/DPDK-Dev/blob/master/dpdk-23.11/app/raft/RAFT.md).

### CloudLab r7525 setup

If you are configuring this repo on CloudLab r7525 using its SmartNIC, please run the following commands to set up the environment. It is based on the [BlueField-2 Quickstart Guide for Clemson R7525s](https://groups.google.com/g/cloudlab-users/c/Xk7F46PpxJo/m/swWhh3LpAwAJ). Please make sure you specify 100Gbs link speed when setting up the experiment.

1. Install the Mellanox rshim driver

```bash
sudo apt update
sudo apt install libfuse2
cd /tmp
wget https://content.mellanox.com/BlueField/RSHIM/rshim_2.0.6-3.ge329c69_amd64.deb
sudo dpkg -i rshim_2.0.6-3.ge329c69_amd64.deb
```

2. Then check whether the rshim service is running:

```bash
sudo systemctl status rshim
```

3. If not running, restart the rshim process

```bash
sudo systemctl restart rshim
```

4. Paste the following line into the text file "/tmp/bf.cfg"

```
ubuntu_PASSWORD='$1$fkQE6cZQ$KlSSiH4HDNTui53W/1hA40'
```

4. Download the bfb image from mellanox

```bash
wget https://content.mellanox.com/BlueField/BFBs/Ubuntu20.04/DOCA_v1.2.1_BlueField_OS_Ubuntu_20.04-5.4.0-1023-bluefield-5.5-2.1.7.0-3.8.5.12027-1.signed-aarch64.bfb
```

5. Install the bfb image to the BF-2 adapter

```bash
sudo bfb-install --bfb /tmp/DOCA_v1.2.1_BlueField_OS_Ubuntu_20.04-5.4.0-1023-bluefield-5.5-2.1.7.0-3.8.5.12027-1.signed-aarch64.bfb --config /tmp/bf.cfg --rshim rshim0
```

6. Reboot the host

```bash
sudo reboot
```

7. To access the BlueField-2 adapter, first set the IP of the tmfifo_net0 interface

```bash
sudo ifconfig tmfifo_net0 192.168.100.1 netmask 255.255.255.0
```

8. SSH to the bluefield-2 adapter

```bash
ssh ubuntu@192.168.100.2
```

Password: Ubuntu1!

9. To make sure the BlueField-2 can access the internet, you can run the following command:

**It should be noted that the following commands are optional and we do not recommend using them since it is connecting the BlueField-2 to the internet throught NAT**

On the Host(This is to set up NAT):

```bash
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o eno1 -j MASQUERADE
sudo iptables -A FORWARD -i enp129s0f0np0 -s 10.10.1.0/24 -j ACCEPT
sudo iptables -A FORWARD -o enp129s0f0np0 -d 10.10.1.0/24 -j ACCEPT
```

You can change `eno1` and `enp129s0f0np0` to your own network interface name.

On the BlueField-2:

```bash
sudo ip addr add 10.10.1.2/24 dev p0
sudo ip link set p0 up
```

**End of the Optional Steps**

Please check your network configuration on the BlueField-2. Run `ip addr show p0` (or other network interface name you used) and see whether it is controlled by the ovs-system.

#### If it is not controlled by the ovs-system
It is the most ideal case. You can setup p0 directly. Run the following commands:

```bash
sudo ip link set p0 up
sudo ip addr add 10.214.96.200/23 dev p0
```
 The ip address `10.214.96.200` is just an example. You can change it to your own value. Then you can change the route configuration by deleting any wrong NAT routes and add p0 to the routing table. For example:

```bash
sudo ip route del default via 192.168.100.1 dev tmfifo_net0
sudo ip route add default via 10.214.96.1 dev p0
```

#### If it is controlled by the ovs-system
You can find `ovs-system` in the result, and you cannot setup p0 directly. You can separate p0 from the ovs-system by running the following command:

```bash
sudo ovs-vsctl del-port ovsbr1 p0
```
and follow the steps in the [previous section](#if-it-is-not-controlled-by-the-ovs-system).

Or if you would like to use the OVS bridge, in which situation the DPDK application might not run properly. Run the following command:

```bash
ip addr show
```

Then look for the same MAC address as p0. Suppose we have `ovsbr1` controlling p0. Then we can set up `ovsbr1` as follows:

```bash
sudo ip link set ovsbr1 up
sudo ip addr add 10.214.96.201/23 dev ovsbr1
```
Then fix the routing table as follows:


```bash
sudo ip route del default via 192.168.100.1 dev tmfifo_net0
sudo ip route add default via 10.214.96.1 dev ovsbr1
```


After setting up the network above, run the following commands:

```bash
echo "nameserver 8.8.8.8" | sudo tee /etc/resolv.conf
echo "nameserver 1.1.1.1" | sudo tee -a /etc/resolv.conf
```

Then ping `8.8.8.8` to make sure the internet is accessible, and use `ip route show` to check the routing table. A quick note: if you have problem with `apt update`, you can try `echo 'Acquire::ForceIPv4 "true";' | sudo tee /etc/apt/apt.conf.d/99force-ipv4` to force apt to use IPv4.

### Build DPDK

Please make sure you are under `dpdk-23.11` before running the following commands.

```bash
meson setup build
ninja -C build
```

Examples are not built by default. You can run `meson configure build -Dexamples=all` or specify which examples to build before using `ninja`. Moreover, if your own machine has other packages not installed, please install them as meson will tell you.

### Scripts

If you are using a NIC with Mellanox driver (like DPU), you do not need to bind the NIC to DPDK driver. Please skip the first step and adjust the script in reboot setup accordingly!

First go to the `script` directory and check `bind-NIC.sh`:

#### Telemetry probe

`script/monitor_probe.sh` relies only on `/proc`, `ping`, `awk`, and sysfs counters to sample CPU %, memory %, peer-to-peer P99 latency, and optional NIC throughput. It writes the most recent snapshot to `dpdk-23.11/app/raft/data_storage/telemetry.json`, while `telemetry.jsonl` and `telemetry.csv` retain history for later plotting. Peer IPs are auto-discovered from `dpdk-23.11/app/raft/config.json`, or you can pass repeated `--peer <id:ip>` overrides.

Example (collect once, include throughput for `ens3`):

```bash
./script/monitor_probe.sh --iface ens3 --once
```

For continuous monitoring, omit `--once` and optionally raise `--interval` (default 10s) to trade accuracy for even lower overhead. Use `--no-csv` or `--no-jsonl` if you only need a specific output format.

```bash
DPDK_DIR=
PCI_DEVICES=
```

Change the two variables to your own values.
You can use `./dpdk-devbind.py -s` in `/dpdk-23.11/usertools` to check your PCI devices and their current driver bindings.

Then check `get-hugepage-available.sh` and change the

```bash
EXAMPLE=
```

variable to your own value. You can check current hugepage status using `cat /proc/meminfo | grep -i huge`

You can run these two scripts and they should have correct printout in the terminal.

### Set up Initial Setup for Reboot

Please `cd` back to root and check `initialize.sh`. Please change the path to your scripts directory in the `initialize.sh` file first. Then run the following command:

```bash
sudo nano /etc/systemd/system/dpdk-bind.service
```

Then copy the following content into the file:

```ini
[Unit]
Description=DPDK Hugepage + NIC Init
After=network-pre.target

[Service]
Type=oneshot
ExecStart=/users/Jiaxi/DPDK-Dev/initialize.sh ;Change to your own path
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Then Reload system and enable the service.

```
sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable dpdk-bind.service
```

You can reboot the system and check whether hugepage and NICs setup successfully.

## Run and Write DPDK Applications

You can run DPDK applications in the `dpdk-23.11/build/examples` directory after building DPDK.

### Write Your Own DPDK Application

You can write your own DPDK application by creating a new directory in the `dpdk-23.11/examples` directory. You can use the existing examples as a reference.

After writing your application, you should include a `meson.build` file in your application directory. Here is an example of a simple `meson.build` file:

```meson
allow_experimental_apis = true
sources = files(
        'main.c',
)
```

Then please add the example name to the `dpdk-23.11/examples/meson.build` file.

After that, if you did not have `meson configure build -Dexamples=all` before, please use `meson configure build -Dexamples+=my_app` to add your application to the build. Then you can just run `ninja -C build` to build your application. Then you can run your application in the `dpdk-23.11/build/examples` directory.

If you would like to write under other directories, please make sure you have included the correct path in the `meson.build` file in the corresponding directory and make the same operation as above to add your application to the build. My raft app is written under the `app` folder for your referrence as well.
