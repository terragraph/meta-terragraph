SUMMARY = "Facebook STATS common utilities"
DESCRIPTION = "Common utilities used by multiple stats modules"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-stats"
SRC_URI = "file://src/common \
           file://LICENSE \
          "

S = "${WORKDIR}/src/common"

DEPENDS = "stats-if"

inherit cmake tgdev
