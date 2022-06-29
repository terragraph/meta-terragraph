DESCRIPTION = "Facebook folly"
HOMEPAGE = "https://github.com/facebook/folly"
SECTION = "core"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=66ebf2d74d339900710c1f776cd41035"

SRCREV = "d60894bf24c2f4e0950335786e6e0aeb3aa97371"
SRC_URI = "git://github.com/facebook/folly.git;protocol=https;branch=main \
           file://folly-force-creation-of-TypeInfo-symbols.patch \
           file://no-int128.patch \
           file://folly-deps-exclude-linked-libraries-from-INTERFACE.patch \
           file://Don-t-use-thread-local.patch; \
          "

S = "${WORKDIR}/git"

inherit cmake

DEPENDS += " \
    boost \
    double-conversion \
    gflags \
    glog \
    jemalloc \
    libevent \
    openssl \
    snappy \
    zstd \
    xz \
    fmt \
"

# Bitbake finds these because they are already in the image,
# but they are not needed for the fbthrift compiler.
DEPENDS_append_class-target = " libaio liburcu"

# Cross compilation fails to find these.  Default them to OFF.
EXTRA_OECMAKE_append = " -DFOLLY_HAVE_UNALIGNED_ACCESS_EXITCODE=OFF"
EXTRA_OECMAKE_append = " -DFOLLY_HAVE_UNALIGNED_ACCESS_EXITCODE__TRYRUN_OUTPUT=''"
EXTRA_OECMAKE_append = " -DFOLLY_HAVE_LINUX_VDSO_EXITCODE=OFF"
EXTRA_OECMAKE_append = " -DFOLLY_HAVE_LINUX_VDSO_EXITCODE__TRYRUN_OUTPUT=''"
EXTRA_OECMAKE_append = " -DFOLLY_HAVE_WCHAR_SUPPORT_EXITCODE=OFF"
EXTRA_OECMAKE_append = " -DFOLLY_HAVE_WCHAR_SUPPORT_EXITCODE__TRYRUN_OUTPUT=''"
EXTRA_OECMAKE_append = " -DHAVE_VSNPRINTF_ERRORS_EXITCODE=OFF"
EXTRA_OECMAKE_append = " -DHAVE_VSNPRINTF_ERRORS_EXITCODE__TRYRUN_OUTPUT=''"
EXTRA_OECMAKE_append= "  -DFOLLY_HAVE_WEAK_SYMBOLS_EXITCODE=0"
EXTRA_OECMAKE_append= "  -DFOLLY_HAVE_WEAK_SYMBOLS_EXITCODE__TRYRUN_OUTPUT=''"
EXTRA_OECMAKE_append= "  -DPACKAGE_VERSION=''"

# Build shared library
CXXFLAGS += "-fPIC"
EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
FILES_SOLIBSDEV = ""
FILES_${PN} += "${libdir}"

BBCLASSEXTEND = "native nativesdk"
