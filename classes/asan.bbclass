# Class file to enable settings for ASAN targets

python () {
  if d.getVar('ENABLE_ASAN', True) == '1':
    d.setVar("DEPENDS_append", " gcc-sanitizers")
    d.setVar("RDEPENDS_append_${PN}", " gcc-sanitizers")
    d.setVar("EXTRA_OECMAKE_append", "-DASAN=ON")
}
