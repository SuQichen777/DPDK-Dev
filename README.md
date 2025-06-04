## Initial Setup
After cloning this repository, please do the following to make sure everything works. If the steps below are not permitted, please try `chmod +x` on the scripts.
### Scripts
First go to the `scripts` directory and check `bind-NIC.sh`:
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