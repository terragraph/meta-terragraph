do_install_append() {
  # Delete *.exe files
  rm -f ${D}${libdir}/python3.7/site-packages/setuptools/*.exe
}
