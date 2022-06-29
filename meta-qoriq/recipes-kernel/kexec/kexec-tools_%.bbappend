FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "\
	file://kdump.conf  \
	file://kdump-patch \
	"

do_patch_kdump() {
    patch -p1 < kdump-patch
}

do_patch_append() {
    bb.build.exec_func('do_patch_kdump', d)
}

# Move kdump to earlier to runlevel chain
INITSCRIPT_PARAMS_kdump = "start 41 S ."

