FILESEXTRAPATHS_prepend := "${THISDIR}/gflags:"

SRC_URI += "file://validator-unused-warnings.patch"

BBCLASSEXTEND = "native"
