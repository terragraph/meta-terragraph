# Watchdog
This document describes the design and implementation of scripts and helper
applications that implement unit-level, autonomous state of health monitoring in
Terragraph nodes. This monitoring system detects, repairs, and logs various
faults.

## General Watchdog Concepts

### Hardware watchdog
The hardware watchdog is a feature of ARM, NXP, and most other CPUs. When
enabled, a watchdog register is decremented every clock cycle, and when it
reaches zero or rolls over, the processor resets.

To prevent the hardware watchdog from resetting, software needs to keep writing
to it. This reset prevention is called "kicking the watchdog". Normally the
watchdog kicks are deliberately stopped to ensure a certain reset, but sometimes
Linux crashes, resulting in a surprise reset. Terragraph detects and logs
"dirty" resets, and also collects context logs.

### Software watchdog
The [software watchdog] (i.e. watchdog daemon) is a fairly standard Linux
application that kicks the hardware watchdog every second. Other [software
watchdog actions] include monitoring memory, disk space, temperature, ping, etc.
(refer to the "man" pages in the software watchdog distribution). If the
software watchdog detects an unrepairable failure, it kills all processes,
unmounts all partitions, and stops kicking the hardware watchdog in order to
ensure a certain reset.

The watchdog daemon can also execute user-defined test/repair scripts, and
Terragraph uses this feature to perform platform-specific monitoring. The daemon
uses the exit code from the test scripts to decide what action to take.

### Watchdog CLI script
The watchdog CLI, `/etc/init.d/watchdog.sh`, is a Bash script that starts and
stops the watchdog daemon, and also performs Terragraph-specific watchdog state
changes. It is based on the [wd_keepalive.init] Yocto init script.

### Test/repair scripts
The test/repair scripts are located in `/etc/watchdog.d/`, and are executed
concurrently by the watchdog daemon every second. There are four Terragraph
test/repair scripts (some of these execute several tests sequentially):
* `link_monit.sh`
* `pop_reachable_monit.sh`
* `progress_monit.sh`

The daemon invokes each test/repair script with the following arguments: `test`
or `repair <error_code>`.
* The `test` invocation of the scripts return 0 to indicate success, or a
  non-zero error code to indicate failure.
* The `repair` invocation of the monitoring scripts only happens on errors.
* The watchdog daemon passes the error code from the `test` invocation to the
  `repair` invocation.
* The `repair` invocation can request a reboot by exiting with a non-zero value.

## Terragraph Watchdog

### CPU core affinity
The watchdog daemon and the scripts that it invokes all run on one particular
CPU core selected explicitly in the `startdaemon()` function in the
`watchdog.sh` CLI script.

### Configuration
Watchdog-related configuration files are shown in the table below.

| Config File            | Description |
| ---------------------- | ----------- |
| `/etc/monit_config.sh` | Parameters for each Terragraph watchdog test (e.g. deadlines, indicator file names). |
| `/etc/watchdog.conf`   | Linux watchdog daemon configuration (e.g. timeouts for the test and repair phases, invocation interval). Note that if a test/repair script blocks for too long, the daemon reboots. |

### Scripts and helper applications
Other watchdog-related scripts and applications are shown in the table below.

| Script/Application                 | Type   | Description |
| ---------------------------------- | ------ | ----------- |
| `get_pop_ip`                       | Lua    | Get the IP of every known PoP node from Open/R's KvStore. |
| `link_monit.sh`                    | Bash   | Test/repair script that performs multiple tests. |
| `monit_init.sh`                    | Bash   | Reset the watchdog state on reboot and on e2e_minion restart. |
| `monit_utils.sh`                   | Bash   | Helper functions. |
| `monotonic-touch`                  | C      | Set and retrieve `CLOCK_MONOTONIC_RAW` file timestamps. |
| `pop_reachable_monit.sh`           | Bash   | Test/repair script that performs multiple tests. |
| `progress_monit.sh`                | Bash   | Test/repair script that performs multiple tests. |
| `persist_event_cache.sh`           | Bash   | Save/load untransmitted events persisted in flash (including watchdog events). |
| `persist_reboot_history.sh`        | Bash   | Manage `/data/log/reboot_history` log. |
| `persist_remaining_gps_timeout.sh` | Bash   | Save/load the remaining GPS fault timeout in flash. |
| `pop_unreachable_cmd.py`           | Python | Collect traceroute logs when no PoP node is reachable. |
| `restart_e2e_minion.sh`            | Bash   | Start and stop e2e_minion with retries. |
| `testcode`                         | Bash   | Manage the "testcode" upgrade state, and display TG image info. |
| `tg_shutdown`                      | Bash   | Invokes persist_xxx scripts at shutdown and rotates logs. |
| `watchdog.sh`                      | Bash   | CLI for watchdog daemon and Terragraph tests |
| `/usr/sbin/watchdog`               | C      | The linux watchdog daemon. |

### E2E software interaction with the watchdog
The E2E minion interacts with the watchdog as shown in the table below.

| E2E minion module | Watchdog interaction |
| ----------------- | -------------------- |
| `WatchdogUtils` helper class | Encapsulates watchdog functionality (set empty `--watchdog_path` flag to disable minion/watchdog interactions) |
| `Progress` helper class | Sets file timestamps of indicator files to the current monotonic time, using the same clock (`CLOCK_MONOTONIC_RAW`) as the monotonic-touch watchdog helper application. Creates the indicator files when necessary. |
| `Minion` main event loop | <ul><li>Reports event loop progress.</li><li>If the e2e_minion event loop is dead, then the watchdog does not intervene when firmware health messages cease, and lets the runsv services monitor restart e2e_minon (and reload the baseband firmware).</li><li>If the e2e_minion event loop is running *and* firmware health messages cease, then the watchdog assumes that the baseband firmware has crashed, or the datapath from the firmware to Linux is dead - and the watchdog intervenes, collects logs, and restarts e2e_minion (along with the baseband firmware). See the Watchdog Fault Table for more details.</li></ul> |
| `ArmDriverIf` | `processFwHealthyMessage()` reports firmware and gps health to the watchdog. |
| `StatusApp` | <ul><li>`processRebootNode()` rejects unforced reboot command in the "testcode" upgrade states. Reboot in this state could cause a surprise image rollback. Note that the "testcode" states are monitored by the watchdog.</li><li>`processStatusReportAck()` reports controller status acknowledgments to the watchdog. These indicate that the node is "connected" to the controller.</li></ul> |
| `UpgradeApp` | `processMessage()` temporarily disables most Terragraph watchdogs during each upgrade phase. This reduces the possibility of dangerous flash corruptions (e.g. during bootloader update), and also avoids losing upgrade states. Note that the watchdog automatically re-enables itself when the disable period expires. |

### Platform software interaction with the watchdog
The platform software interacts with the watchdog as shown in the table below.

| Platform software | Watchdog interaction |
| ----------------- | -------------------- |
| Wi-Fi (Puma) message server | `messageHandler()` temporarily disables most Terragraph watchdogs while processing commands. Wireless command messages include a field for the watchdog disable time period. There is also a standalone wireless command (`WATCHDOG`) to temporarily disable or re-enable the watchdog. Note that the watchdog automatically re-enables itself when the disable time period expires. |
| `flash_mtd.sh` startup script | <ul><li>Kicks off configuration fallback monitoring when the current startup/reboot was *required* to apply the new configuration.</li><li>Performs configuration rollback in different image fallback states:<ol><li>No upgrade or fallback is in progress.</li><li>The current startup/reboot reverted the upgrade image, and need to roll back to the pre-upgrade configuration. See the Watchdog Fault Table for more information about "testcode" faults.</li></ol><li>Note that the primary function of the `flash_mtd.sh` startup script is to (re)initialize and mount the `/data` flash partition.</li></li></ul> |

### Locks
[flock] is used by the watchdog for the following purposes:
* Ensure that repair actions don't run concurrently.
* Ensure that only one watchdog thread/script shuts down.
* Synchronize state updates which can happen from different contexts. For
  example, the watchdog can be disabled for a fixed time period by a user and
  also by the E2E minion during upgrades; config rollback states are set by the
  watchdog and also by the `ConfigApp` in the E2E minion.

Indicator files are used for the following purposes:
* Prevent the re-execution of a test script before the corresponding repair
  invocation is done (this is a feature/bug in the watchdog daemon).
* Skip the testing logic by returning 0 ("OK") to the daemon when a
  shutdown/reboot is in progress.

### Watchdog state
The runtime state of the watchdog is comprised of indicator files and a few data
files in `/var/volatile/progress`. The state is mostly a collection of
timestamps of various past events, and some deadlines. File timestamps are used
to keep event/deadline times using a monotonic clock (`CLOCK_MONOTONIC_RAW`).
Scripts use the `monotonic_touch` helper application to "touch" indicator
files with current or future monotonic time.

Some watchdog state (e.g. remaining GPS timeout, unsent watchdog events) needs
to be persisted across reboots, and is saved temporarily in `/data` at shutdown.

### Test/repair scripting considerations

#### Don't write to stdout (or stderr) from the test/repair scripts

Most of the test/repair functions write "0" or "1" to stdout to indicate
success/failure. Random output by linux commands can result in surprise test
failures and repair failures causing a reboot.

```bash
# Unsafe
sv restart fib_nss

# Safe
sv restart fib_nss >/dev/null 2>/dev/null
```

#### Set a timeout for commands that may take a while in test/repair scripts

The test and repair phases both have deadlines set in `/etc/watchdog.conf`.
The watchdog daemon reboots when these deadlines are exceeded.

```bash
# Unsafe
save_gps_version.sh /dev/ttyS1

# Safe (timeout values here are only examples)
timeout -k 1 10 save_gps_version.sh /dev/ttyS1
if [ $? -ne 0 ]; then
  # Oops. Timed out. Handle timeout explicitly.
fi
```

### Debugging the watchdog
Three methods for debugging the watchdog are described below.

#### Monitor test/repair script errors
Error logs are kept for both the test and the repair phases
in `/var/log/watchdog`. These error log files are normally empty, which means
that there were no script errors.

```bash
$ ls -l /var/log/watchdog/
total 4
-rw-r--r-- 1 root root   0 Jun 30 20:57 repair-bin.stderr
-rw-r--r-- 1 root root   0 Jun 30 20:57 repair-bin.stdout
-rw-r--r-- 1 root root   0 Jun 30 20:57 test-bin.stderr
-rw-r--r-- 1 root root   0 Jun 30 20:41 test-bin.stdout
```

#### Use the debug logs
The test/repair scripts report internal states on every invocation. This
debug logging functionality is implemented by `monit_debug()` in
`monit_config.sh`. The debug logs are test-script specific, have a monotonic
timestamp, and appear in `/data/log/wdog`. Note that these logs are not rotated
and **can fill up the flash**.

Debug logging can be enabled/disabled in two ways:
```bash
# Enable/disable debug logging until the next reboot
$ /etc/init.d/watchdog.sh en_logs
$ /etc/init.d/watchdog.sh dis_logs

# Enable/disable debug logging permanently
$ touch /data/log/wdog/wdog_log_enable
$ rm -f /data/log/wdog/wdog_log_enable
```

#### Observe the operator watchdog logs and the watchdog state
The normal reboot and watchdog logs are helpful for debugging issues:
* `/var/log/wdog_repair_history`
* `/var/log/wdog_repair_history.2`
* `/data/log/reboot_history`
* `/data/log/reboot_history.2`

To observe most of the runtime state, execute the following command (note that
`monotonic-touch -t` prints the current monotonic time):
```bash
$ watch -n 1 'monotonic-touch -t; for f in $(find /var/volatile/progress -type f | sort); \
  do echo -n "$f ";stat -c %Y $f; done'
```

### Test entry points
The "entry point" is the name of the main bash function for each test. Note that
some tests have helper functions, and others are implemented by test logic
embedded in the main `test_xxx()` function that is called when the test script
is invoked with the "test" argument by the watchdog daemon.

#### Puma watchdog test entry points
| Description | Script | Bash Function | Notes |
|-------- | ------ | ------------- | ----- |
| Monitor the "testcode" upgrade state. | `progress_monit.sh` | `test_testcode_fallback()` | In the "testcode" state, the unit is running ("testing") an upgrade image from the secondary boot partition. A reboot for any reason will cause a rollback to the original image in the primary boot partition. This watchdog reboots if the new image (specifically, `e2e_minion`) fails to connect to the controller by a deadline. |
| Re-enable the disabled watchdogs. | `progress_monit.sh` | `timed_enable_tgscripts()` | Most watchdogs can be disabled safely for a fixed period of time via the CLI by running `/etc/init.d/watchdog.sh dis <minutes>`. This watchdog re-enables the disabled watchdogs at the deadline set by the CLI. |
| Monitor GPS sanity. | `progress_monit.sh` | `test_gps()` | See the Watchdog Fault Table for details. |
| A baseband card was unable to form RF links for about 15 minutes. | `progress_monit.sh` | `test_prog()` | See the Watchdog Fault Table for details. |
| Baseband firmware or datapath timeout/crash. | `progress_monit.sh` | `test_prog()` | There are two different failure modes. See the Watchdog Fault Table for details. |
| Monitor `/tmp` full. | `pop_reachable_monit.sh` | `test_fsys()` | n/a |
| Monitor `/data` full. | `pop_reachable_monit.sh` | `test_fsys()` | n/a |
| Roll back bad configuration (specifically, new node configuration that prevents connecting to the controller). | `pop_reachable_monit.sh` | `test_config_fallback()` | There are several different failure modes. See the Watchdog Fault Table for details. |
| Test PoP node reachability. | `pop_reachable_monit.sh` | `test_pop_reachable()` | See the Watchdog Fault Table for details. |
| No RF peer is reachable, although some RF links are up. | `link_monit.sh` | `test_link()` | n/a |

## Resources
* [software watchdog] - Linux software watchdog
* [software watchdog actions] - Linux software watchdog actions
* [wd_keepalive.init] - Yocto watchdog keepalive daemon manager
* [flock] - Manage locks from shell scripts

[software watchdog]: https://sourceforge.net/projects/watchdog/
[software watchdog actions]: https://linux.die.net/man/8/watchdog
[wd_keepalive.init]: https://github.com/openembedded/openembedded-core/blob/master/meta/recipes-extended/watchdog/watchdog/wd_keepalive.init
[flock]: http://man7.org/linux/man-pages/man1/flock.1.html
[kexec]: https://linux.die.net/man/8/kexec
