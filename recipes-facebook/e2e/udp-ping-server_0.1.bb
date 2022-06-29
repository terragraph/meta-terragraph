SUMMARY = "Facebook UDP Ping Server"
DESCRIPTION = "Facebook UDP Ping Server"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-e2e"

SRC_URI = "file://e2e/ping"

S = "${WORKDIR}/e2e/ping"

inherit cmake tgdev

DEPENDS = "folly gflags glog"
