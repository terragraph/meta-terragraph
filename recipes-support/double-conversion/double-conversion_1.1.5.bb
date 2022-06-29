DESCRIPTION = "Efficient binary-decimal and decimal-binary conversion routines for IEEE doubles"
HOMEPAGE = "https://github.com/google/double-conversion/"

LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=1ea35644f0ec0d9767897115667e901f"

# v1.1.5
SRCREV = "57b1e094dbd12152f4daf8c6e91184fabf603906"
SRC_URI = "git://github.com/google/double-conversion.git;protocol=https;branch=branch_v1.1 \
          "

S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE_append = " -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON"

CXXFLAGS_append = " -fPIC "

FILES_${PN} = "/usr"

BBCLASSEXTEND = "native"
