#!/bin/sh
#
# Configures and starts firmware logging with host_manager_11ad on all devices.
#
# Configures host_manager_11ad log directory, binary string path and polling
# interval, then starts collecting both firmware and microcode logs for all
# interfaces recognized by host_manager_11ad. The host_manager_11ad utility
# must be running first.

# shellcheck source=/dev/null
. /usr/sbin/config_get_env.sh

/usr/bin/shell_11ad log_collector set_config \
    LogFilesDirectory="/var/log/wil6210/" \
    ConversionFilePath="/data/firmware/wil6210/" \
    PollingIntervalMs=10 \
    MaxNumberOfLogFiles="${FW_LOGGING_FILE_COUNT:-10}" \
    MaxSingleFileSizeMb="${FW_LOGGING_FILESIZE_MB:-10}"

# V - Verbose, I - Info, W - Warn, E - Error
if [ "${FW_LOG_VERBOSE:-0}" -eq "0" ]; then
    VERBOSITY="IWE"
elif [ "$FW_LOG_VERBOSE" -eq "1" ]; then
    VERBOSITY="E"
elif [ "$FW_LOG_VERBOSE" -eq "2" ]; then
    VERBOSITY="WE"
elif [ "$FW_LOG_VERBOSE" -eq "3" ]; then
    VERBOSITY="IWE"
elif [ "$FW_LOG_VERBOSE" -eq "4" ]; then
    VERBOSITY="VIWE"
fi

# Automatically start log recording on any future interfaces that come up.
# This will not start recording on any current interfaces.
/usr/bin/shell_11ad log_collector start_deferred_recording DefaultVerbosity="$VERBOSITY" RecordingType=txt Compress=true

# Find all current interfaces and start log recording on them.
INTERFACES=$(/usr/bin/shell_11ad host get_interfaces | grep Interfaces: | sed -e 's/Interfaces://;s/,//g')
for iface in $INTERFACES; do
    /usr/bin/shell_11ad log_collector set_verbosity DefaultVerbosity="$VERBOSITY" CpuType=fw Device="$iface"
    /usr/bin/shell_11ad log_collector set_verbosity DefaultVerbosity="$VERBOSITY" CpuType=ucode Device="$iface"
    /usr/bin/shell_11ad log_collector start_recording RecordingType=txt Compress=true Device="$iface"
done
