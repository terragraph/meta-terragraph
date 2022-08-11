# Service Scripts
This document describes Terragraph's service scripts and some related utilities.

## Cloud Services
Terragraph manages its cloud services on x86 hosts using [systemd].

### systemd scripts
All systemd scripts reside in `/etc/tg_systemd_config/`. The actual service
scripts are expected to be installed on the host manually, while the start/stop
scripts remain in the rootfs. More details about the systemd scripts can be
found in `src/terragraph-e2e/systemd/README.md`.

The set of files used in each service is as follows:
```
/etc/tg_systemd_config/
  <service>.service       - systemd service file (to be installed on host)
  <service>.start         - start script
  <service>.docker.start  - start script (when running in Docker container)
  <service>.stop          - stop script
```

<a id="service-scripts-node-services"></a>

## Node Services
Services on Terragraph nodes are managed using [runit].

<a id="service-scripts-runit-scripts"></a>

### runit scripts
All runit scripts reside in `/etc/sv/<service>/` on the nodes. Most will run by
default (e.g. `e2e_minion`, `openr`), some are disabled (e.g. `e2e_controller`),
and others run only once after boot (e.g. `pop_config`). Many of these scripts
can be enabled or disabled via node configuration; a common pattern in the `run`
scripts is to sleep until seeing an "enabled" flag in the configuration.

Logs from each service are written to `/var/log/<service>/current` and are
automatically rotated.

The basic directory structure for each service is as follows:
```
/etc/sv/<service>/
  run     - start script
  finish  - shutdown script
  down    - if present, service is disabled by default
  log/
    run   - log script (executes svlogd)
```

The runit framework is started via `/etc/init.d/runit` (installed from
`recipes-utils/runit/files/runit.init`). Some noteworthy points are below:
* `runsv` needs to write to `/etc/sv/`, but this is on a read-only partition. To
  handle this, scripts are actually installed in `/etc/sv.bak/`, then copied to
  `/var/run/sv/` during runtime. A symbolic link is created from `/etc/sv` to
  `/var/run/sv`.
* `runsvdir` is executed with `taskset` to run all applications on the last
  available core only.

### Initializing drivers, firmware, and interfaces
There are several scripts called when starting either `e2e_minion` (via
`/usr/sbin/e2e_minion_wrapper.sh`) or `driver_if_daemon` (via
`/usr/sbin/driver_if_start.sh`):
1. `/usr/sbin/fb_tg_load_common.sh` - Provides functions to run, stop, and
   restart a service. The `_run` function calls `fb_load_dr_fw.sh`.
2. `/usr/bin/fb_load_dr_fw.sh` - Provides a function to load the drivers and
   firmware, then bring up link interfaces, among other things. This calls the
   vendor-specific script `fb_load_bh_drv.sh`.
3. `/usr/bin/fb_load_bh_drv.sh` - Provides functions to load the wireless
   ("backhaul") driver.

A pair of helper scripts is provided to associate a point-to-point link using
`r2d2` and `driver_if_daemon`:
* `/usr/bin/tfdn_r2d2.sh` - Used on the initiator node
* `/usr/bin/tfcn_r2d2.sh` - Used on the responder node

<a id="service-scripts-environment-variables"></a>

### Environment variables
Most environment variables are loaded in scripts via
`/usr/sbin/config_get_env.sh`, which exports fields in the `envParams` node
configuration structure. This will call `/usr/sbin/config_read_env` to generate
an intermediate file, `/data/cfg/config`, as needed.

Additional vendor-specific environment variables, mostly relating to kernel
modules, are static and contained in `/usr/bin/tg.env`.

Hardware-related information is written to `/var/run/node_info`, which is
generated at boot time by `/etc/init.d/gen_node_info_file.sh`. The node info
file is required by several services (e.g. `e2e_minion`). Most fields in this
file are read from EEPROM. The file contents are as follows:
```bash
NODE_ID="node ID, must be a MAC address (ex. nic0 or wlan0)"
TG_IF2IF="use IF2IF firmware files (0 or 1)"
NUM_WLAN_MACS="number of radios"
MAC_X="baseband MAC address X"
BUS_X="PCI bus X"
GPIO_X="GPIO X (default: -1)"
NVRAM_X="NVRAM X (default: bottom_lvds)"
PCI_ORDER="ordering of pci slots for interface indexing"
HW_MODEL="hardware model string"
HW_VENDOR="hardware vendor"
HW_BOARD_ID="hardware board ID"
HW_REV="hardware revision number"
HW_BATCH="hardware batch number"
HW_SN="hardware serial number"
```
Note that on Puma hardware when using the PMD, the default MAC address is
[read from OTP](Driver_Stack.md#driver-stack-mac-address-assignment), and these
usually do not match the fields in EEPROM (i.e. `MAC_X` above). Some services
will write and read a modified node info file in `/tmp/node_info` that contains
the correct WLAN MAC addresses.

### Custom startup scripts
User scripts can be executed as part of the boot sequence by placing them in
`/data/startup/`. This is done via `/etc/init.d/data_startup.sh`. For example,
the following lines may be helpful for development:
```bash
# Disable software watchdog (to avoid unexpected reboots or service restarts)
source /etc/monit_config.sh
mkdir -p "$progress_dir"
disable_all_tg_scripts

# Make root writeable (if manually modifying files in read-only paths)
mount -o remount, rw /
```

### System time
For systems without an RTC module (ex. Puma), there may be several time jumps as
the system boots:
* The system time is initialized using the `/etc/timestamp` file. This file is
  written at build time during rootfs generation, and holds the build date
  (format: `+%4Y%2m%2d%2H%2M%2S`).
* The `chronyd` time daemon runs via an `/etc/init.d/` init script. This will
  synchronize system time to NTP servers defined in node configuration
  (`sysParams.ntpServers`), or `time.facebook.com` by default. It will also use
  GPS/PPS as a time source if configured (`envParams.GPSD_PPS_DEVICE`,
  `envParams.GPSD_NMEA_TIME_OFFSET`). Logs are written to `/var/log/chrony/`.
* The `time_set` script runs via an `/etc/init.d/` init script after at least 90
  seconds of system uptime. This attempts to run `chronyd -q` (one-time clock
  step, similar to `ntpdate`) to NTP servers in the `oob` (out-of-band) network
  namespace. Logs are written to `/tmp/time_set.log`.

### Syslog
Nodes run the `rsyslogd` daemon to configure system logging via
`/etc/rsyslog.conf`. Some standard files are disabled to save space. Note that
any boot-time syslog messages are dropped if they are written before `rsyslogd`
has started (e.g. in the init.d sequence).

### Log rotation
Log rotation is managed using the `logrotate` utility, and application-specific
configuration is located in the `/etc/logrotate.d/` directory. `logrotate` is
triggered periodically via the cron job `/etc/cron.d/logrotate`. Note that a
daily logrotate entry for `/data/log/logs.tar.gz` will save an archive of
`/var/log/` into the persistent `/data/` directory.

## Scripting Languages
The sections below briefly describe the scripting languages used in Terragraph
and how they are set up.

### Shell
Most included shell scripts are compliant with POSIX shell, but some require
Bash. Code is validated using [ShellCheck].

### Python
Terragraph installs Python 3.8, with the interpreter at `/usr/bin/python3`
(along with related links) and core/user packages at `/usr/lib/python3.8/`. In
`terragraph-image-minimal`, only `.pyc` files are kept (`.py` and `.opt-*` files
are deleted).

Code is auto-formatted with [Black] and [isort] (config: `.isort.cfg`), and is
linted using [Flake8] (config: `.flake8`). Python tests are installed to
`terragraph-image-x86` and run using [ptr] (config: `.ptrconfig`) via
`meta-x86/recipes-testing/ptr/files/run_ptr.sh`.

Python code is not critical for production. To exclude Python and all dependent
packages from an image, build with `conf/no-python.conf`.

### Lua
Terragraph installs Lua 5.2, with all user scripts and dependencies captured in
`recipes-facebook/e2e/e2e-files-lua_0.1.bb`. Library code is minified using
[LuaMinify]. The installed file structure is as follows:
```
/usr/bin/{lua,luac}             - Lua interpreter/compiler
/usr/sbin/*.lua                 - user scripts
/usr/sbin/tests/lua/*_test.lua  - unit tests (X86 only)
/usr/lib/lua/5.2/               - library path (not /usr/share/lua/)
/usr/lib/liblua*.so*            - Thrift .so libraries (others are in the standard library path)
```

Code is validated using [Luacheck] (config: `.luacheckrc`). Code documentation
is auto-generated using [LDoc] (config: `.config.ld`). Lua tests are written
using [LuaUnit] and installed to `terragraph-image-x86`.

Note that user scripts are generally *not* compatible with Lua 5.1/5.3+ or
LuaJIT. However, they are mostly functional in Lua 5.1 after installing the
[compat52] module (see `recipes-support/lua-compat52/lua-compat52_0.3.bb`) and
importing it globally via `require("compat52")`, for example at the beginning of
the `tg.utils` module (`src/terragraph-e2e/lua/tg/utils.lua`).

## Resources
* [systemd] - Linux init system with service management
* [runit] - UNIX init scheme with service supervision
* [ShellCheck] - Shell script static analyzer
* [Black] - Python code formatter
* [isort] - Python import organizer
* [Flake8] - Python linter
* [ptr] - Python test runner
* [LuaMinify] - Lua code minifier
* [Luacheck] - Lua static analyzer and linter
* [LDoc] - Lua documentation generator
* [LuaUnit] - Lua unit testing framework
* [compat52] - Lua compatibility module providing Lua-5.2-style APIs for Lua 5.1

[systemd]: https://www.freedesktop.org/wiki/Software/systemd/
[runit]: http://smarden.org/runit/
[ShellCheck]: https://www.shellcheck.net/
[Black]: https://github.com/psf/black
[isort]: https://github.com/timothycrosley/isort
[Flake8]: https://gitlab.com/pycqa/flake8
[ptr]: https://github.com/facebookincubator/ptr
[LuaMinify]: https://github.com/stravant/LuaMinify
[Luacheck]: https://github.com/mpeterv/luacheck
[LDoc]: https://github.com/stevedonovan/LDoc
[LuaUnit]: https://github.com/bluebird75/luaunit
[compat52]: https://github.com/keplerproject/lua-compat-5.2
