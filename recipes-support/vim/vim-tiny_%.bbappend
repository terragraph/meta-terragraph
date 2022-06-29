FILESEXTRAPATHS_prepend := "${THISDIR}/vim:"

SRC_URI += "file://vimrc"

do_install_append() {
  install -D -m 0755 ${WORKDIR}/vimrc ${D}${datadir}/vim/vimrc
}

FILES_${PN} += " \
  ${datadir}/vim/vimrc \
  "
