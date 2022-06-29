# Firmware Versioning
This document summarizes the versioning scheme for Terragraph's wireless
firmware.

## QTI Firmware
The section below is specific to QTI (Qualcomm Technologies Inc.) wireless
firmware.

### Version Numbers
QTI firmware releases are tied to a specific Terragraph software release.
See the table below for information about some past firmware releases.

| FW Version | Marketing Name                            | TG Version | TG Branch            |
| ---------- | ----------------------------------------- | ---------- | -------------------- |
| 10.11.x.99 | DPDK Consumer Sample R2 (DPDK-OEMR2-CSU4) | M44.21     | -                    |
| 10.11.x.92 | DPDK Consumer Sample R2 (DPDK-OEMR2-CSU3) | M44.20     | -                    |
| 10.11.x.87 | DPDK Consumer Sample R2 (DPDK-OEMR2-CSU2) | M44.19     | -                    |
| 10.11.x.83 | DPDK Consumer Sample R2 (DPDK-OEMR2-CSU)  | M44.18     | -                    |
| 10.11.x.77 | DPDK Consumer Sample R2 (DPDK-OEMR2-CS)   | M44.17     | -                    |
| 10.11.x.70 | DPDK Consumer Sample Update 2 (DPDK-CSU2) | M44.17     | -                    |
| 10.11.x.66 | DPDK Consumer Sample Update (DPDK-CSU)    | M44.16     | -                    |
| 10.11.x.59 | DPDK Consumer Sample (DPDK-CS)            | M44.16     | -                    |
| 10.11.x.55 | DPDK Engineering Sample (DPDK-ES)         | M44.15     | -                    |
| 10.11.x.28 | Consumer Sample Update (CSU)              | M44.12     | integration/m44/cs   |
| 10.11.x.9  | Commercial Sample (CS)                    | M44.11     | integration/m44/cs   |
| 10.9.x.y   | Feature Complete (FC)                     | M44.11     | integration/m44/fc   |
| 10.7.x.y   | Engineering Sample (ES)                   | M44.8      | integration/m44/es   |

The firmware version string can be extracted as follows:
```bash
$ /usr/sbin/get_fw_version
10.11.0.92

$ strings /lib/firmware/wil6436.fw | grep version
FW version: 10.11.0.92
```

Within each QTI release, there is a separate firmware version with format
`major.minor.sub_minor.build` (as defined in
`meta-qca/recipes-radio/wigig-utils-private/files/fw_ver.ini`). The `build`
number is incremented on each change to the `major.minor` release. `sub_minor`
can take the following values:
```
0 - Operational (RF), 1 - WMI_ONLY, 2 - NO_PCIE, 3 - WMI_ONLY_NO_PCIE, 4 - IF2IF
```

For example, the DPDK-OEMR2-CSU3 release contains RF firmware
(`TALYN_M_B0/wil6210.fw`) with version 10.11.0.92, and IF2IF firmware
(`TALYN_M_B0_IF2IF/wil6210.fw`) with version 10.11.4.92.

### Release Packages
Releases from QTI contain several packages:
* OEMs receive `device`, `3pp`, and `tg-src`.
* Meta additionally receives `fb-src`.

The contents include, among other things:
* A firmware binary (`wil6210.fw`) for each `sub_minor` type.
* A set of corresponding board files (`*.brd`) for each firmware binary. The
  default for QRP (QTI Reference Platform) is named `wil6436.brd`, which is
  a copy of another board file in the same directory.
* Firmware strings (`fw_image_trace_string_load.bin`) required for decoding
  firmware logs using any of the provided tools (e.g. `host_manager_11ad`,
  `wil_fw_trace`). The strings are only available under special license from
  QTI.

### Terragraph Layout
On the Terragraph software image, the files mentioned above are installed to
`/lib/firmware`, with additional links created as follows (using the Puma
platform as an example):
```
[read-only]
/lib/firmware/
  wil6436.fw -> TALYN_M_B0/wil6210.fw             # default RF firmware
  wil6436.brd -> TALYN_M_B0/<production_*>.brd    # "golden" board file
  TALYN_M_B0/
    wil6436_puma.brd -> ./<production_*>.brd      # Puma default RF board file
  TALYN_M_B0_IF2IF/
    wil6436_puma.brd -> ./<production_*>.brd      # Puma default IF2IF board file

[read-write]
/data/firmware/wil6210/                           # firmware_class.path
  fw_image_trace_string_load.bin -> /lib/firmware/<TALYN_M_B0*>/fw_image_trace_string_load.bin
  ucode_image_trace_string_load.bin -> /lib/firmware/<TALYN_M_B0*>/ucode_image_trace_string_load.bin
  wil6436.fw -> /lib/firmware/<TALYN_M_B0*>/wil6210.fw
  wil6436.brd -> /lib/firmware/<TALYN_M_B0*>/wil6436_puma.brd
```

The driver searches for firmware and board files in `/data/firmware/wil6210`
first (as specified by `firmware_class.path`), then in `/lib/firmware`. The
read-write path enables run-time switching of firmware and board files, e.g. to
swap between RF and IF2IF operation. Much of this logic is handled in
`/usr/bin/fw_brd_update.sh`.

When searching for board files, the driver first looks for the file
`<root>/<pci_id>/wil6436.brd`, then `<root>/wil6436.brd`. The former enables the
use of "native" board files calibrated for each per baseband card (in RF mode
only), whereas the latter represents the "golden" board file used as a fallback.

When `envParams.FW_USE_NATIVE_BOARDFILE` is set to "1" in the node
configuration, then "native" board files are loaded from EEPROM via
`/usr/bin/bb_fetch_brdfile.sh` and are installed by `fw_brd_update.sh` as
follows:
```
[read-write]
/data/firmware/
  wil6210/
    <pci_id0>/
      wil6436.brd       # board file for PCI slot pci_id0
    <pci_id1>/
      wil6436.brd       # board file for PCI slot pci_id1
    ...
  native -> ./wil6210   # link for compability with bb_fetch_brdfile.sh
```
