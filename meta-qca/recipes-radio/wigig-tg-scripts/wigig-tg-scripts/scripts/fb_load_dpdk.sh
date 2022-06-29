#!/bin/sh

# shellcheck disable=SC1091
. tg.env

fb_bind_vfio_drivers()
{
    # shellcheck disable=SC2016
    parse_command='
    /^Slot:/ {
        if (slot != "")
            print slot, driver
        slot = $2
        driver = ""
    }
    /^Driver:/ { driver = $2 }
    END {
        if (slot != "")
            print slot, driver
    }
    '
    lspci -Dvmmnnk -d 17cb:1201 |
    awk "${parse_command}" |
    while read -r pciid driver
    do
        # If current driver is already correct, do nothing
        if [ "${driver}" = "vfio-pci" ]
        then
            continue
        fi

        # If there is any other driver, unbind it
        if [ -n "${driver}" ]
        then
            echo "${pciid}" > "/sys/bus/pci/drivers/${driver}/unbind"
        fi

        # Bind new driver to the device
        echo "Binding ${pciid} to vfio-pci"
        echo "vfio-pci" > "/sys/bus/pci/devices/${pciid}/driver_override"
        echo "${pciid}" > "/sys/bus/pci/drivers/vfio-pci/bind"
    done
}

fb_load_dpdk()
{
  FW_BUSID="$1"
  if [ ! -z "${FW_BUSID}" ]; then
    # nothing to do here
    return
  fi

  # initialize the dpdk driver
  echo "Loading $DPDK_MOD_NAME"
  modprobe "${DPDK_MOD_NAME}" "pci_order=${PCI_ORDER}"

  fb_bind_vfio_drivers
}

fb_unload_dpdk()
{
  # kill DPDK application processes
  killall wiltest
  killall vpp
  killall dpdk-pktgen

  # remove modules
  modprobe -r "${DPDK_TG_MOD_NAME}"
  modprobe -r "${DPDK_MOD_NAME}"
}

fb_set_bb_macs()
{
  # HACK! modify radio MAC addresses in node info file to use BB MACs (not EEPROM)
  TMP_E2E_NODE_INFO_FILE="/tmp/node_info"
  if [ ! -f "$TMP_E2E_NODE_INFO_FILE" ]; then
    echo "Can't find $TMP_E2E_NODE_INFO_FILE"
    return 1
  fi

  # delete possibly conflicting lines
  sed -i -E '/^(NUM_WLAN_MACS|MAC_[0-9]+|BUS_[0-9]+)=.*$/d' "$TMP_E2E_NODE_INFO_FILE"

  # write new lines by finding MACs on current terra links
  FW_NUM_OF_LINKS="$1"
  TERRA_LINKS="$(ip -o link show | grep terra | awk '{print $2, $(NF-2)}')"
  lua -e "
    -- Parse inputs
    local pciOrder = {}
    for s in ('$PCI_ORDER'):gmatch('[^,]+') do
      -- Chop off :00.0 for consistency with existing format in EEPROM
      pciOrder[#pciOrder+1] = s:sub(-5) == ':00.0' and s:sub(1, -6) or s
    end
    local terraLinksPerRadio = tonumber('$FW_NUM_OF_LINKS')
    local macs, buses = {}, {}
    for s in ([[$TERRA_LINKS]]):gmatch('[^\\r\\n]+') do
      local mac = s:match('%s+(.+)$')
      local terraIdx = tonumber(s:match('terra([0-9]+)'))
      local busIdx = math.floor(terraIdx / terraLinksPerRadio)
      if macs[busIdx] == nil then
        macs[busIdx] = mac
        buses[#buses+1] = busIdx
      end
    end

    -- Print output
    print(string.format('NUM_WLAN_MACS=\"%d\"', #buses))
    for _, busIdx in ipairs(buses) do
      print(string.format('MAC_%d=\"%s\"', busIdx, macs[busIdx]))
      if pciOrder[busIdx + 1] ~= nil then
        print(string.format('BUS_%d=\"%s\"', busIdx, pciOrder[busIdx + 1]))
      end
    end
  " >> $TMP_E2E_NODE_INFO_FILE
}

fb_dpdk_wait_for_dev_ready()
{
    DEV_TIMEOUT_S="$1"

    BB_COUNT=$(lspci -d 17cb:1201 | wc -l)
    TIME_ELAPSED=0
    DEV_SLEEP_TIME_S=2
    while true; do
        DEVS_UP=$(find /sys/class/net/ -name "dhd*" | wc -l)
        if [ "$DEVS_UP" -lt "$BB_COUNT" ]; then
            if [ "$DEVS_UP" -gt 0 ] && [ "$TIME_ELAPSED" -ge "$DEV_TIMEOUT_S" ]; then
                echo "Proceeding without all dhd devices (counted $DEVS_UP of $BB_COUNT after $TIME_ELAPSED seconds)"
                break
            fi
            echo "Waiting for dhd devices (counted $DEVS_UP of $BB_COUNT)"
            sleep "$DEV_SLEEP_TIME_S"
            if [ "$DEVS_UP" -gt 0 ]; then
                TIME_ELAPSED=$((TIME_ELAPSED + DEV_SLEEP_TIME_S))
            fi
        else
            break
        fi
    done
    TERRA_COUNT=$((DEVS_UP * FW_NUM_OF_LINKS))
    while true; do
        TERRA_UP=$(ip -o link show | grep -c terra)
        if [ "$TERRA_UP" -lt "$TERRA_COUNT" ]; then
            echo "Waiting for terra interfaces (counted $TERRA_UP of $TERRA_COUNT)"
            sleep 1
        else
            break
        fi
    done
}
