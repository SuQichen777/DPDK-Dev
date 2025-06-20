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

#### Build DPDK
```bash
meson setup build
meson configure build -Dexamples=all # You can also specify which examples to build
ninja -C build
```
If your own machine has other packages not installed, please install them as meson will tell you.

### Scripts
First go to the `script` directory and check `bind-NIC.sh`:
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

If you would like to write under other directories, please make sure you have included the correct path in the `meson.build` file in the corresponding directory and make the same operation as above to add your application to the build.