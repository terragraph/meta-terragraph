SUMMARY = "Facebook Terragraph Kernel Module for 20130"
DESCRIPTION = "Facebook Terragraph Kernel Module for 20130"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

require kernel-module-terragraph.inc

TG_MODNAME_SUFFIX = "dhd"
export TG_MODNAME_SUFFIX

# Build driver to use common BH interface
TG_ENABLE_BH_COMMON = "y"
export TG_ENABLE_BH_COMMON
