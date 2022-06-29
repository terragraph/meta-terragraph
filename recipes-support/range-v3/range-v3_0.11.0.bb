DESCRIPTION = "C++ Ranges"
HOMEPAGE = "https://github.com/ericniebler/range-v3"
SECTION = "core"
LICENSE = "BSL-1.0 & MIT & Stepanov-and-McJones-EoP & SGI-CPP-STL"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=5dc23d5193abaedb6e42f05650004624"

SRC_URI = "https://github.com/ericniebler/range-v3/archive/refs/tags/${PV}.tar.gz;downloadfilename=${BP}.tar.gz"

SRC_URI[md5sum] = "97ab1653f3aa5f9e3d8200ee2a4911d3"

S = "${WORKDIR}/${BP}"

inherit cmake

DEPENDS += " boost"

CXXFLAGS += "-fPIC"
CXXFLAGS += "-Wno-unused-result"
EXTRA_OECMAKE_append = " -DBUILD_WITH_LTO=OFF"
EXTRA_OECMAKE_append = " -DBUILD_SHARED_LIBS=ON"
EXTRA_OECMAKE_append = " -DBUILD_TESTS=OFF"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
