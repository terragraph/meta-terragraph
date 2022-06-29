DESCRIPTION = "Facebook Open/R routing libraries and tools."
HOMEPAGE = "https://github.com/facebook/openr"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

include openr_src.inc

# This rev is a parent of SRCREV and does not include backwards compatibility
# changes for M80 and pre-M80 interoperation. This revision is preferred because
# it contains only a few changes compared to the Open/R trunk.
BACKWARDS_INCOMPATIBLE_REV = "780adcd855bbb597e8f33bee7447bd20423f1fba"

S = "${WORKDIR}/git"

inherit cmake tgdev

DEPENDS += " \
  fbthrift \
  fbthrift-compiler-native \
  openssl \
  folly \
  fbzmq \
  gmock \
  gtest \
  jemalloc \
  libsodium \
  libnl \
  range-v3 \
  re2 \
  fb303 \
  "

RDEPENDS_${PN} += " \
  bash \
  fbthrift \
  fbzmq \
  jemalloc \
  "

CXXFLAGS += "-fPIC"
CXXFLAGS += "-Wno-unused-result"
EXTRA_OECMAKE_append = " -DBUILD_WITH_LTO=OFF"
EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=OFF"
EXTRA_OECMAKE_append = " -DBUILD_TESTS=OFF"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
EXTRA_OECMAKE_append = " -DFBTHRIFT_COMPILER=${WORKDIR}/recipe-sysroot-native/usr/bin/thrift1"
EXTRA_OECMAKE_append = " -DFBTHRIFT_TEMPLATES_DIR=disable"
EXTRA_OECMAKE_append = " -DBUILD_TOOLS=OFF"

FILES_${PN}-tests += "/usr/sbin/openr_kvstore_snooper \
                      /usr/sbin/netlink_socket_sample \
                      "

FILES_${PN} += "${libdir}/*.so"
FILES_SOLIBSDEV = ""
FILES_${PN}-dev += " ${libdir}/*.h ${libdir}/*.tcc"

addtask do_backwards_compat after do_patch before do_configure

do_backwards_compat() {
  cd ${S}
  test "${OPENR_NO_BACKWARDS_COMPAT}" == "true" && \
  git checkout "${BACKWARDS_INCOMPATIBLE_REV}" || \
  git checkout "${SRCREV}"
}

do_install_append() {
  # Rename Open/R binary
  mv ${D}/usr/sbin/openr_bin ${D}/usr/sbin/openr

  mkdir -p ${D}/usr/include/openr/if
  cp -r ${B}/openr/if/gen-cpp2 ${D}/usr/include/openr/if/gen-cpp2

  # Thrift structs that come from configerator are not needed in the
  # space-optimized Open/R build.
  test "${OPENR_TG_OPTIMIZED_BUILD}" != "true" && \
  mkdir -p ${D}/usr/include/configerator/structs/neteng/config && \
  cp -r ${B}/configerator/structs/neteng/config/gen-cpp2 \
      ${D}/usr/include/configerator/structs/neteng/config/gen-cpp2 && \
  cp -r ${S}/configerator ${D}/usr/include/;

  # Install TG-specific OpenrConfig.thrift. The thrift structs in the original
  # OpenrConfig.thrift have extra features not needed in the space-optimized
  # Open/R build. Compiled thrift contributes significantly to overhead in
  # libraries and binaries, so we used a reduced OpenrConfig.thrift file.
  test "${OPENR_TG_OPTIMIZED_BUILD}" == "true" && \
  cp ${S}/openr/if/tg/OpenrConfig.thrift ${D}/usr/include/openr/if/;
}
