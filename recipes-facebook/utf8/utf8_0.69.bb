SUMMARY = "add en_USE UTF-8 environment to default profile"
DESCRIPTION = "Add en_US LANG, LANGUAGE + LC_ALL environment variables to /etc/profile"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${META_TERRAGRAPH_DIR}/licenses/Meta-MIT;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://locale.conf \
           file://locale.sh"

S="${WORKDIR}"

do_install(){
 install -d ${D}${sysconfdir}/profile.d/
 install -m 0777 ${S}/locale.conf ${D}${sysconfdir}
 install -m 0777 ${S}/locale.sh ${D}${sysconfdir}/profile.d/
}

FILES_${PN} = "${sysconfdir}/*"
