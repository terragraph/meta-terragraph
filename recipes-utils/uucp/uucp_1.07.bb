SUMMARY = "UNIX uucp package"
DESCRIPTION = "unix uucp package"
HOMEPAGE = "https://airs.com/ian/uucp.html"
SECTION = "base"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://COPYING;md5=94d55d512a9ba36caa9b7df079bae19f"

SRC_URI = "ftp://ftp.gnu.org/pub/gnu/uucp/uucp-1.07.tar.gz \
           file://fprintf-security-warning.patch \
"

SRC_URI[md5sum] = "64c54d43787339a7cced48390eb3e1d0"
SRC_URI[sha256sum] = "060c15bfba6cfd1171ad81f782789032113e199a5aded8f8e0c1c5bd1385b62c"

FILES_${PN} = "${bindir}/ ${sbindir}/ /usr/spool/uucp/"

inherit autotools

# For uucp user
DEPENDS += "base-passwd"

do_install_append() {
	install -d -m 755 -o uucp -g root ${D}/usr/spool/uucp
	touch ${D}/usr/spool/uucp/.empty
}
