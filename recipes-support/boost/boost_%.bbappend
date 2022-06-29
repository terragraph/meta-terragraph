# Pull boost-locale and boost-python for large space savings.
PACKAGECONFIG = ""

# We need to force Boost::Context built even for native build
# because upstream forces it to be removed for the consistency
# sake on all host platforms and this only can be overridden
# in the custom python code, since _remove modifier takes
# precendence over all other variable manipulation constructs
# Doing it from python has another advantage - it does nothing
# if 'context' is already in BOOST_LIBS
# This will no longer be needed as of gatesgarth
python __anonymous () {
    packages = d.getVar('BOOST_PACKAGES').split()
    extras = d.getVar("BJAM_EXTRA").split()
    libs = d.getVar('BOOST_LIBS').split()
    lib = "context"
    if lib not in libs:
        libs.append(lib)
        extras.append("--with-%s" % lib)
        pkg = "boost-%s" % lib.replace("_", "-")
        packages.append(pkg)
        if not d.getVar("FILES_%s" % pkg):
                d.setVar("FILES_%s" % pkg, "${libdir}/libboost_%s*.so.*" % lib)
    d.setVar("BOOST_LIBS", " ".join(libs))
    d.setVar("BOOST_PACKAGES", " ".join(packages))
    d.setVar("BJAM_EXTRA", " ".join(extras))
}
