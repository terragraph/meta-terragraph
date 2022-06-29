require wigig-fw-release-bin.inc

# Pull in the latest OEM firmware.  Use the fw-str version with the
# debug strings if you have it, otherwise just use the 3pp file
# also used by wigig-utils.
require tg-qca6430-DPDK-OEMR2-CSU3-2021-06-29-OEM-ver-10-11-0-92-3pp.inc

# Add a custom board file for Puma hardware
SRC_URI += "file://murata_1d_generic.brd;subdir=${WIGIG_RELEASE_DIR}/TALYN_M_B0"

PUMA_BOARD_FILE_RF = "murata_1d_generic.brd"
PUMA_BOARD_FILE_IF = "production_YCC334_1x1_xif1_28_0x3009CC0D.brd"
