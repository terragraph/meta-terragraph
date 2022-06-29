FILESEXTRAPATHS_append := "${THISDIR}/files:${THISDIR}/files/src/breeze:"

SRC_URI += "file://breeze.patch \
            file://fib_vpp_cli_lib.py;subdir=git/openr/py/openr/cli/clis \
           "
