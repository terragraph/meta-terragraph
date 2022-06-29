do_install_append() {
  # Delete *.exe files
  rm -f ${D}${libdir}/python3.8/distutils/command/*.exe
}
