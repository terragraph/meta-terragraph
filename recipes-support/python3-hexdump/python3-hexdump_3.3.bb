DESCRIPTION = "Dump binary data to hex format and restore from there"
HOMEPAGE = "https://bitbucket.org/techtonik/hexdump/"
SECTION = "devel/python"
LICENSE = "PD"
LIC_FILES_CHKSUM = "file://PKG-INFO;beginline=8;endline=8;md5=b46c242890afc1ecfe0faa5b6dcf2a69"

SRCNAME = "hexdump"

inherit setuptools3

SRC_URI = "https://pypi.python.org/packages/source/h/hexdump/${SRCNAME}-${PV}.zip \
           file://0001-Do-not-install-the-data-file.patch"
SRC_URI[md5sum] = "0bf05d40e1af5abea6939215dfcb9105"
SRC_URI[sha256sum] = "d781a43b0c16ace3f9366aade73e8ad3a7bd5137d58f0b45ab2d3f54876f20db"

S = "${WORKDIR}"
