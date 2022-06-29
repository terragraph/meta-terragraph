# Engineering drop firmware with debug strings
# These are not official Qualcomm releases and are for testing purposes only.

SRC_URI = "http://localhost/EngDrop_10.11.82.zip"

SRC_URI[md5sum] = "d10a225d88c092db30bdcef5bbc1e2f2"
SRC_URI[sha256sum] = "33f11bf33d24a9a48f5d7e33ff7127db32793b52de54dae0dea57c24e05e4b77"

WIGIG_RELEASE_DIR="10.11.82"

SRC_URI += "file://murata_1d_generic.brd;subdir=${WIGIG_RELEASE_DIR}/TALYN_M_B0"

PUMA_BOARD_FILE_RF = "murata_1d_generic.brd"
PUMA_BOARD_FILE_IF = "production_YCC334_1x1_xif1_28_0x3009CC0D.brd"
