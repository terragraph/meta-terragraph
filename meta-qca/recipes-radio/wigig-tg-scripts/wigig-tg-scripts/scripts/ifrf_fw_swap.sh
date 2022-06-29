#! /bin/sh
#
# Switch RF or IF firmware
#
# THIS IS DEPRECATED!
# Use /usr/bin/fw_brd_update.sh instead (called during system init).
# To check IF2IF mode in the digital board EEPROM, run these commands:
#   db_eeprom=$(fdtget /sys/firmware/fdt /chosen eeprom)
#   fdtget "$db_eeprom" /board tg-if2if
# To toggle IF2IF mode, run these commands and then call the script mentioned
# above:
#   db_eeprom=$(fdtget /sys/firmware/fdt /chosen eeprom)
#   fdtput -p "$db_eeprom" -t i /board tg-if2if <0|1>

# Parse args
FW=$( echo "$1" | awk '{print toupper($0)}' )
if [ "$FW" = "IF" ]; then
  FWDIR="/lib/firmware/TALYN_M_B0_IF2IF"
fi
if [ "$FW" = "RF" ]; then
  FWDIR="/lib/firmware/TALYN_M_B0"
fi
if [ -z ${FWDIR} ]; then
  echo "usage: $( basename "$0" ) [rf|if]"
  exit 1
fi

# Validate paths
if [ ! -d ${FWDIR} ]; then
  echo "${FWDIR} not found. Failed to switch to $FW firmware."
  exit 1
fi
BRDFILE="wil6436_puma.brd"
if [ ! -f "${FWDIR}/${BRDFILE}" ]; then
  echo "${FWDIR}/${BRDFILE} not found. Failed to switch to $FW firmware."
  exit 1
fi

# If root is mounted read-only, temporarily remount as read-write
RW_REMOUNT=0
if grep -q "[[:space:]]ro[[:space:],]" /proc/mounts; then
  RW_REMOUNT=1
  mount -o rw,remount /
fi

# Swap firmware files
ln -sf ${FWDIR}/wil6210.fw /lib/firmware/wil6436.fw
ln -sf ${FWDIR}/${BRDFILE} /lib/firmware/wil6436.brd
if [ -f ${FWDIR}/fw_image_trace_string_load.bin ]; then
  ln -sf ${FWDIR}/fw_image_trace_string_load.bin /lib/firmware/fw_image_trace_string_load.bin
fi
if [ -f ${FWDIR}/ucode_image_trace_string_load.bin ]; then
  ln -sf ${FWDIR}/ucode_image_trace_string_load.bin /lib/firmware/ucode_image_trace_string_load.bin
fi

if [ $RW_REMOUNT = "1" ]; then
  mount -o ro,remount /
fi

echo "Switched to $FW firmware"
