# Driver Stack
This document briefly describes Terragraph's driver stack.

## Architecture
The driver stack consists of the Terragraph driver and a wireless ("backhaul")
driver. Terragraph implements its datapath using [DPDK], a framework for fast
packet processing in user space, along with its own implementation of a
`wil6210` Poll Mode Driver (PMD). These components are described below.

### Kernel Modules
The Terragraph driver communicates with underlying hardware using a particular
type of interface, and compiles into a corresponding kernel object file:
* **dhd:** `terragraph-dhd.ko` using the BCM20130 API ("dongle host driver")
* **qwilvendor:** `terragraph-qca.ko` using the wil6210 API

Similarly, there are several wireless drivers available:
* `wil6210.ko` - Wilocity wil6210 kernel driver
* `bcmdhd.ko` - Broadcom BCM20130 driver
* `dpdk-dhd.ko` - DPDK driver (with `wil6210` PMD, described below)

The kernel modules loaded for each Terragraph hardware type are listed below:
* **Rev5:** `terragraph-dhd.ko`+`bcmdhd.ko`
* **Puma:**
  `terragraph-qca.ko`+`wil6210.ko` (Linux kernel-based datapath) *OR*
  `terragraph-dhd.ko`+`dpdk-dhd.ko` (DPDK-based datapath)

### Poll Mode Driver (PMD)
DPDK utilizes Poll Mode Drivers (PMD), which run in the user-space DPDK
environment and continuously poll for data packets.

Terragraph has its own PMD implementation, `wil6210`, for the Talyn2 chip
(wil6436) used by Puma. This is a partial port of the corresponding Linux kernel
driver `wil6210.ko`, and only implements features that are strictly needed.

### DPDK Applications
DPDK relies on user-space applications to link with its libraries and the
`wil6210` PMD. For production, Terragraph uses [VPP] (Vector Packet Processing
framework). For more details, see [VPP Implementation](VPP_Implementation.md).

Other DPDK applications include:
* **[Pktgen]:** Traffic generator
* **[Testpmd]:** Reference application that forwards packets between Ethernet
  ports
* **wiltest:** Test application that forwards packets from Linux `terraX` netdev
  interfaces to the appropriate wireless link and back

## Code Structure
The sections below provide an overview of the driver stack's code structure.

### Kernel Modules
The Terragraph driver sources are located in
`recipes-radio/wireless-mod/files/fb_terragraph/`. These are installed as
follows:
* `terragraph-qca.ko` via
  `recipes-radio/wireless-mod/kernel-module-terragraph-qca_0.1.bb`
* `terragraph-dhd.ko` via
  `recipes-radio/wireless-mod/kernel-module-terragraph-dhd_0.1.bb`

The API between the Terragraph and wireless drivers is defined in the header
file `recipes-radio/wireless-mod/files/nl-driver-if-hdr/fb_tg_backhaul_if.h`.

The wireless drivers are installed as follows:
* `wil6210.ko` via
  `meta-qca/recipes-radio/wigig-utils-oss/kernel-module-wil6210.bb`
* `bcmdhd.ko` via `recipes-radio/broadcom-dhd/kernel-module-bcmdhd_0.1.bb`

The `dpdk-dhd.ko` module sources are fetched with the PMD sources and are
located the in the directory `dpdk/modules/dpdk-dhd/`. The module is installed
along with the `wil6210` PMD.

### Poll Mode Driver (PMD)
The `wil6210` PMD sources are fetched from the same upstream repository as the
`wil6210.ko` kernel driver. PMD sources are in the directory
`dpdk/drivers/wil6210/` while kernel driver sources are in the directory
`wil6210/`. PMD sources are compiled into `librte_pmd_wil6210.a` and installed
via `meta-qca/recipes-radio/wigig-dpdk/wigig-dpdk_git.bb`.

The source files for the PMD are prefixed with `wil6210_`, many of which
are modified copies of similarly-named files (without the prefix) in the
original `wil6210.ko` Linux kernel driver. A compatibility layer,
`wil6210_compat.h`, provides implementations for some Linux-specific APIs since
the PMD runs in a non-Linux environment; this makes it possible to take the
upstream kernel driver code with minimal changes.

A subdirectory, `dpdk-dhd-ctrl/`, contains code to send `ioctl` commands to
`/dev/dhd`, and also to set up `AF_PACKET` queues and required machinery to
exchange requests between kernel and user space.

### DPDK Applications
The DPDK user-space applications are installed as follows:
* **[VPP]:** Installed via `meta-qoriq/recipes-extended/vpp/vpp_19.01-lsdk.bb`.
  For more details, see [VPP Implementation](VPP_Implementation.md).
* **[Pktgen]:** Installed as `dpdk-pktgen` via
  `meta-qca/recipes-extended/pktgen-dpdk/pktgen-dpdk_git.bb`.
* **[Testpmd]:** Not installed by default; part of `dpdk-utils` (see
  `meta-qoriq/recipes-extended/dpdk/dpdk_19.09-lsdk.bb`).
* **wiltest:** Installed via
  `meta-qca/recipes-radio/wigig-dpdk/wigig-dpdk_git.bb`. The sources are located
  in `src/dpdk/examples/wil6210-test/`.

## DPDK Device Arguments
The `wil6210` PMD exposes a set of DPDK device arguments (or "devargs") for
configuration, defined in `wil6210_pcidev.c`. These are described in the table
below. Note that flags are set/enabled with a value of `1` and unset/disabled
with a value of `0`.

| Name                | Default | Description |
| ------------------- | ------- | ----------- |
| `crash-on-fw-err`   | 0*   | Flag to crash if a firmware error occurs. Note that this is *enabled* by default in the driver, but is *disabled* in Terragraph's VPP configuration. |
| `fw-core-dump-path` | `/var/volatile/cores/wil6210_fw_core` | File path prefix to write core dump to after a firmware crash. The PCI ID and date are appended to the provided path. |
| `fw-log-level`      | 0    | Log level of both firmware and microcode logs (0: ERROR+WARN+INFO, 1: ERROR, 2: +WARN, 3: +INFO, 4: +VERBOSE). |
| `fw-log-path`       | null | File path to write logs to while device runs (enables PMD log polling thread). |
| `fw-strings`        | `/data/firmware/wil6210/fw_image_trace_string_load.bin` | File path of firmware binary strings file to read logs. |
| `mac-address`       | null | The MAC address to be used by the device, overriding the MAC address [read from OTP](#driver-stack-mac-address-assignment). |
| `mtu-max`           | 1986 | Maximum MTU. The default value is computed as `TXRX_BUF_LEN_DEFAULT` (2048) - `WIL_MAX_MPDU_OVERHEAD` (62). |
| `no-fw-recovery`    | 1*   | Flag to disable firmware recovery. Recovery is currently supported only in VPP, and not in other DPDK applications. Note that this is *disabled* by default in the driver (i.e. enabling recovery), but is *enabled* in Terragraph's VPP configuration (i.e. disabling recovery). |
| `non-commercial-rf` | 1*   | Flag to determine if non-commercial RF is attached to the device. Note that this is *disabled* by default in the driver, but is *enabled* in Terragraph's VPP configuration. |
| `opaque-log`        | 0    | Flag to enable collection of opaque logs without binary strings file (requires `fw-log-path`/`ucode-log-path`). |
| `p2mp-capable`      | 1    | Flag to enable sending a particular WMI command to firmware for P2MP-capable devices. |
| `pcie-expected-gen` | 0    | Expected PCIe gen value. If it is nonzero and does not match the value read from the device PCIe link status, the driver will retrain the PCIe link before rereading and sending PCIe information to firmware as usual. |
| `pcie-expected-lanes`| 0   | Expected PCIe lane count. If it is nonzero and does not match the value read from the device PCIe link status, the driver will retrain the PCIe link before rereading and sending PCIe information to firmware as usual. |
| `pmc-ext-host`      | 1*   | Flag to enable firmware logging to go into the host buffer from the very beginning of firmware initialization. Otherwise the firmware will use device memory for logging initially until the host configures it to use the ring buffer in host memory. Ensures early boot logs are fully captured. Note that this is *disabled* by default in the driver, but is *enabled* in Terragraph's VPP configuration. |
| `pmc-ext-ring-order`| 10   | Determines the size of the ring in host memory used for recording logs (in 2^order units). |
| `ucode-log-path`    | null | File path to write microcode logs to while device runs (enables PMD log polling thread). |
| `ucode-strings`     | `/data/firmware/wil6210/ucode_image_trace_string_load.bin` | File path of microcode binary strings file to read logs. |

To manually set devargs when running VPP, add them under the VPP configuration
option `dpdk dev <pci_id> devargs` in the VPP startup configuration file
(`/var/run/vpp/startup.conf`). Multiple devargs can be provided as a
comma-separated list. Example:
```
dpdk {
  dev 0000:01:00.0 {
    devargs fw-core-dump-path=/tmp/fw_dump.core,fw-strings=/data/fw_string.bin
  }
}
```

When using other DPDK applications (e.g. pktgen, wiltest), devargs can be passed
as part of the "PCI whitelist" (`--pci-whitelist` or `-w`) EAL argument, also in
a comma-separated list. Example:
```
$ wiltest -w 0001:01:00.0,fw-log-path=/tmp/fw_logs,opaque-log=1
```

## Initialization Procedure
The sections below summarize the driver stack initialization and shutdown
processes using DPDK and the `wil6210` PMD.

### Initialization
#### 1. Load kernel modules
Initially, the `terragraph-dhd.ko` and `dpdk-dhd.ko` kernel modules are loaded:
```bash
$ modprobe terragraph-dhd && modprobe dpdk-dhd
```

`dpdk-dhd.ko` registers `/dev/dhd`, a miscellaneous device instance, during
startup (in `dhd_init()`). It then waits for a user-space DPDK application to
open `/dev/dhd` and issue `ioctl` commands (handled in `dhd_ioctl()`).

The `pci_order` module parameter can be used to specify indexing of PCI devices
for bringing up `dhdX` and `terraX` devices (the first device gets `dhd0`, next
`dhd1`, etc.). When no ordering is specified, naming follows PCI enumeration
order. The parameter value should be comma-separated
`domain:bus:device.function` PCI addresses passed as a string and include all
PCI devices being used. For Puma, the default is:
```bash
$ modprobe dpdk-dhd pci_order="0000:01:00.0,0001:01:00.0,0002:03:00.0,0002:04:00.0"
```

#### 2. Configure hugepages
DPDK requires [hugepages] for the large memory pool allocation used for packet
buffers.
```bash
$ mkdir /mnt/hugepages
$ mount -t hugetlbfs none /mnt/hugepages
```

#### 3. Bind wireless devices via dpdk-devbind
DPDK requires the wireless devices (i.e. baseband cards) to be made available in
user space (and unbound from the Linux kernel), by binding them to either the
[VFIO] or [UIO] kernel driver. The baseband cards are PCIe devices, so they are
bound to the `vfio-pci` driver:
```bash
$ echo "vfio-pci" > "/sys/bus/pci/devices/<pci_id>/driver_override"
$ echo "<pci_id>" > "/sys/bus/pci/drivers/vfio-pci/bind"
```

The role of `vfio-pci` is to provide the necessary kernel support to drive the
hardware from a user-space process safely and efficiently. Specifically,
`vfio-pci` puts the device behind IOMMU (I/O Memory Management Unit) so that it
cannot use DMA to access arbitrary physical memory, and provides `ioctl`
commands to reflect interrupts from the kernel into the DPDK process. This is
well abstracted by DPDK libraries.

#### 4. Start the DPDK application
At this point, a DPDK application is started and loads the `wil6210` PMD, which
looks for all available Talyn PCI devices. The PMD defines the `net_wil6210` PCI
device in `wil6210_pcidev.c`, containing the PCI ID table along with
probe/remove routines.

During the probe, the PMD invokes `wil6210_dev_init()` (resembling
`wil_pcie_probe()` from the `wil6210.ko` kernel driver). Among other tasks, this
loads the firmware, performs other low-level initialization, and invokes
`wil_register_slave()` in `wil6210_slave.c` to expose the `qwilvendor` platform
device. The PMD implements its own `platform_device_*` functions (declared in
the Linux API compatibility layer, `wil6210_compat.h`) in `wil6210_control.c` to
use on this `qwilvendor` device (instead of having it attached to
`terragraph-qca.ko`).

#### 5. Invoke `DPDK_DHD_ATTACH`
Next, the PMD opens `/dev/dhd` and invokes the `ioctl` command `DPDK_DHD_ATTACH`
(using `dhd_attach()` in `dpdk-dhd-ctrl.c`). In response, `dpdk-dhd.ko` creates
a Linux network interface (`dhd0`, `dhd1`, `dhd2`, or `dhd3`) and returns this
information back to user space. The PMD uses this information to set up
`AF_PACKET` queues for the interface.

#### 6. Invoke `DPDK_DHD_START`
The PMD then invokes the `ioctl` command `DPDK_DHD_START` (using `dhd_start()`
in `dpdk-dhd-ctrl.c`). In response, `dpdk-dhd.ko` registers the `terragraph_bh`
platform device, which gets attached to `terragraph-dhd.ko` (via `tg_bh_probe()`
in `fb_tgd_terragraph_linux.c`).

Upon attach, the Terragraph driver creates all of the `terraX` devices, and
registers itself with the Netlink subsystem to handle Terragraph-specific
requests targeting the backhaul device instance. The Terragraph driver also uses
an API defined in `fb_tg_backhaul_if.h` to request services, and `dpdk-dhd.ko`
converts these function calls into control request packets with codes
`DHD_CMDOP_REGISTER` and `DHD_CMDOP_IOCTL` and sends them back to the DPDK
application. The PMD spawns a separate thread to handle these control messages.

This concludes the startup process. The driver stack is ready to accept Netlink
requests from `driver-if`.

### Shutdown
The `ioctl` command `DPDK_DHD_STOP` undoes `DPDK_DHD_START` by unregistering the
`terragraph_bh` platform device. This in turn causes `terragraph-dhd.ko` to
detach from it, shutting down and destroying all `terraX` interfaces and
deregistering from Netlink.

The last `close()` on the `/dev/dhd` file handle undoes `DPDK_DHD_ATTACH` (there
is no special "detach" `ioctl` command).

<a id="driver-stack-mac-address-assignment"></a>

## MAC Address Assignment
When using the PMD, the default MAC address of each baseband card will be the
MAC address read from its one-time-programmable (OTP) memory. This is a unique
MAC address assigned during manufacturing. It is also possible to assign a
different MAC address by passing in a value to the devarg `mac-address` when
starting DPDK applications. To do this for VPP using the MAC address
[read from EEPROM](Service_Scripts.md#service-scripts-environment-variables),
set the node configuration flag `envParams.VPP_USE_EEPROM_MACS`.

When using the Linux kernel driver, the default MAC address assigned to each
baseband card is the MAC address read from EEPROM.

<a id="driver-stack-example-message-path"></a>

## Example Message Path
The example below highlights the message path for a user-initiated command, e.g.
from the `r2d2` CLI or `e2e_minion`.

<p align="center">
  <img src="../media/figures/driver_stack_ioctl.svg" width="850" />
</p>

1. `driver-if` receives a message and uses Netlink to send it to the kernel.
2. The `terragraph-dhd.ko` Netlink handler receives the request, extracts the
   firmware parameter blob, and uses an `ioctl` command to send it to
   `dpdk-dhd.ko`.
3. `dpdk-dhd.ko` wraps the request into a control packet and puts it on the
   `AF_PACKET` queue for the PMD.
4. The PMD event handler thread wakes up, reads the packet data, and hands the
   `DHD_CMDOP_IOCTL` message to `wil_sync_handler()` in `wil6210_control.c`.
   This will invoke the `ioctl` method as implemented by the driver, which uses
   a WMI (Wireless Module Interface) control queue to hand the data to the
   firmware.
5. Eventually, firmware responds with the results, and the reverse process
   occurs: the response is placed into the PMD's TX `AF_PACKET` queue, then
   `dpdk-dhd.ko` receives it and returns from the `ioctl` call, and finally
   `terragraph-dhd.ko` sends the associated response back to `driver-if` over
   Netlink.

## Debugging
The following sections outline how to obtain logs and crash dumps, specifically
with QTI firmware (where applicable).

### Driver Logs
Logs from the Terragraph driver can be found in syslog files
(`/var/log/kern.log`) or via the `dmesg` command. Logging verbosity is
controlled by the node configuration field `envParams.FB_DRIVER_VERBOSE` (see
`E_DBG_ENABLE_VALUE` in `fb_tgd_debug.h`). This bitmask can be viewed and set
dynamically with the following commands, respectively:
```bash
$ cat /sys/module/terragraph_{dhd,qca}/parameters/dbg_mask
$ echo 0x10001 > /sys/module/terragraph_{dhd,qca}/parameters/dbg_mask
```

### Poll Mode Driver (PMD) Logs
Logs from the PMD are contained within the DPDK application logs. For example,
when using VPP, the PMD logs captured by VPP are extracted from syslog and
written to `/var/log/vpp/vnet.log`.

### Firmware/Microcode Logs
There are several methods for collecting firmware and microcode logs:
* `host_manager_11ad` - This service runs automatically and writes logs to
  `/var/log/wil6210/` when the `envParams.FW_LOGGING_ENABLED` node configuration
  field is set. This is done through `start_host_manager_fw_log.sh`, which uses
  the `shell_11ad` CLI to configure the `host_manager_11ad` daemon. Logs are
  rotated and compressed, with retention set by the node configuration fields
  `envParams.FW_LOGGING_FILE_COUNT` and `envParams.FW_LOGGING_FILESIZE_MB`.
* PMD thread - When using VPP, a separate log polling thread can be enabled in
  the PMD by passing the devargs `fw-log-path` and `ucode-log-path` for firmware
  and microcode logs, respectively.
* `wil_fw_trace` - An alternative logging utility. To collect logs for *all*
  Talyn devices, use the script `run_wil_fw_trace.sh`. Example usage:
```bash
$ wil_fw_trace -d 0001:01:00.0 -s /data/firmware/wil6210/fw_image_trace_string_load.bin -v VERBOSE
```

When no strings file is present, logs can be collected in opaque/binary form
instead, then decoded later using a matching strings file. To enable collection
of opaque logs, either omit the strings parameter (`-s`) to `wil_fw_trace`, or
pass the devarg `opaque-log=1` (along with `fw-log-path`) when using the PMD log
polling thread. Decode the opaque logs using the `-O` flag to `wil_fw_trace`:
```bash
$ wil_fw_trace -m /path/to/opaque_logs -s /data/firmware/wil6210/fw_image_trace_string_load.bin -O
```

Separate settings control the verbosity of QTI firmware logs and Facebook
firmware logs (categorized as `FW_3P`):
* QTI firmware verbosity is set statically using the "DefaultVerbosity"
  parameter in `shell_11ad`, or the `--verbosity` flag in `wil_fw_trace`. When
  using `envParams.FW_LOGGING_ENABLED`, the `host_manager_11ad` service sets the
  verbosity according to the node configuration field
  `envParams.FW_LOG_VERBOSE`.
* Facebook firmware verbosity can be changed during runtime via the
  `FW_SET_LOG_CONFIG` command, e.g. using the CLI command
  `tg2 minion fw_set_log_config`.

### Firmware Core Dumps
When using the PMD, core dumps are automatically written to
`/var/volatile/cores/wil6210_fw_core_<pci>_<date>` after a firmware crash
occurs. This path is configurable via the devarg `fw-core-dump-path`.

Firmware logs will automatically be read from the core dump, if possible, into
the file `<core-name>_trace.log`. This uses the default strings file
`/data/firmware/wil6210/fw_image_trace_string_load.bin`, which can be changed
via the devarg `fw-strings`. Microcode logs cannot be read from the core dump.
To manually read firmware logs from a core dump, use the following
`wil_fw_trace` command:
```bash
$ wil_fw_trace -m /path/to/core -s /data/firmware/wil6210/fw_image_trace_string_load.bin -o 0x1a01c0 -l 2
```

When using the kernel driver, firmware core dumps must be manually generated by
running the script `wil6210_core_dump.sh` after a firmware crash is observed.

### Kernel Crashes
Terragraph handles kernel crashes (panics) by saving the kernel message log
(dmesg) to log files and rebooting. The logs are saved under
`/data/kernel_crashes/vmcore.<date>` (configured via
`meta-qoriq/recipes-kernel/kexec/files/kdump.conf`).

This works using kexec/kdump as follows. As part of a normal startup sequence,
the `/etc/init.d/kdump` script is executed. It registers a dump kernel (also
called `crashkernel`, located at `/boot/Image-<version>-kdump`) with the running
(original/regular) kernel to be executed in case of a crash. When a crash
happens, the original kernel executes the registered crashkernel using kexec,
without rebooting, which means that the memory contents of the original kernel
remains available to the crashkernel. The crashkernel runs a regular userspace,
including the `/etc/init.d/kdump` script. The script notices that it's running
as part of a crashkernel (by the presence of a `/proc/vmcore` file), gets the
original kernel messages (dmesg) from memory, saves them to
`/data/kernel_crashes` and reboots. This time, it's a regular reboot that goes
through the boot loader (rather than kexec), and loads the original kernel as
usual.

## Resources
* [DPDK] - Data Plane Development Kit
* [VPP] - Vector Packet Processing
* [Pktgen] - DPDK Packet Generator
* [Testpmd] - DPDK Testpmd Application
* [hugepages] - Linux hugetlbpage support
* [VFIO] - Virtual Function I/O
* [UIO] - Userspace I/O

[DPDK]: https://www.dpdk.org/
[VPP]: https://wiki.fd.io/view/VPP
[Pktgen]: https://pktgen-dpdk.readthedocs.io/en/latest/
[Testpmd]: https://doc.dpdk.org/guides/testpmd_app_ug/
[hugepages]: https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt
[VFIO]: https://www.kernel.org/doc/Documentation/vfio.txt
[UIO]: https://www.kernel.org/doc/html/latest/_sources/driver-api/uio-howto.rst.txt
