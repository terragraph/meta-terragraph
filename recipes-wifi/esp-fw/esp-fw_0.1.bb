SUMMARY = "Terragraph ESP32 wifi f/w"
DESCRIPTION = "Terragraph ESP32 wifi f/w"
SECTION = "base"

# The esp-idf components are covered by the following licenses.
LICENSE = "Apache-2.0 & BSD-2-Clause & BSD-3-Clause & BSL-1.0 & GPLv2 & ISC & MIT"
LIC_FILES_CHKSUM = " \
    file://${WORKDIR}/src/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640 \
    file://${WORKDIR}/esp-idf/LICENSE;md5=3b83ef96387f14655fc854ddc3c6bd57 \
    file://${WORKDIR}/esp-idf/components/asio/asio/asio/COPYING;md5=3e73f311a3af69e6df275e8c3b1c09b5 \
    file://${WORKDIR}/esp-idf/components/aws_iot/aws-iot-device-sdk-embedded-C/LICENSE.txt;md5=acc7a1bf87c055789657b148939e4b40 \
    file://${WORKDIR}/esp-idf/components/bt/lib/LICENSE;md5=3b83ef96387f14655fc854ddc3c6bd57 \
    file://${WORKDIR}/esp-idf/components/coap/libcoap/LICENSE.BSD;md5=c3ba00f24fff294132e7cca4eea68a6d \
    file://${WORKDIR}/esp-idf/components/esp32/lib/LICENSE;md5=3b83ef96387f14655fc854ddc3c6bd57 \
    file://${WORKDIR}/esp-idf/components/esptool_py/esptool/LICENSE;md5=b234ee4d69f5fce4486a80fdaf4a4263 \
    file://${WORKDIR}/esp-idf/components/expat/expat/expat/COPYING;md5=5b8620d98e49772d95fc1d291c26aa79 \
    file://${WORKDIR}/esp-idf/components/json/cJSON/LICENSE;md5=218947f77e8cb8e2fa02918dc41c50d0 \
    file://${WORKDIR}/esp-idf/components/libsodium/libsodium/LICENSE;md5=7f5ecba1fa793fc1f3c8f32d6cb5a37b \
    file://${WORKDIR}/esp-idf/components/lwip/lwip/COPYING;md5=59a383b05013356e0c9899b06dc5da3f \
    file://${WORKDIR}/esp-idf/components/mbedtls/mbedtls/LICENSE;md5=302d50a6369f5f22efdb674db908167a \
    file://${WORKDIR}/esp-idf/components/mbedtls/mbedtls/apache-2.0.txt;md5=3b83ef96387f14655fc854ddc3c6bd57 \
    file://${WORKDIR}/esp-idf/components/micro-ecc/micro-ecc/LICENSE.txt;md5=5660ea7c0fedb3e43cd1bc79497cb8a7 \
    file://${WORKDIR}/esp-idf/components/mqtt/esp-mqtt/LICENSE;md5=a650c8b075579254231da018af1a2072 \
    file://${WORKDIR}/esp-idf/components/nghttp/nghttp2/COPYING;md5=764abdf30b2eadd37ce47dcbce0ea1ec \
    file://${WORKDIR}/esp-idf/components/protobuf-c/protobuf-c/LICENSE;md5=cb901168715f4782a2b06c3ddaefa558 \
    file://${WORKDIR}/esp-idf/components/spiffs/spiffs/LICENSE;md5=8d7bbd9158dc6195456ad2b309af02e7 \
    "

# Prerequisites
#
#  See ESP32_TOOLCHAIN_PREFIX below for the expected ESP32 toolchain path.
#
#  See the ESP-IDF Programming Guide for ESP32 toolchain installation:
#     https://docs.espressif.com/projects/esp-idf/en/stable/get-started/linux-setup.html

SRC_URI = "\
 git://github.com/espressif/esp-idf;protocol=https;name=idf;nobranch=1;destsuffix=esp-idf \
 git://github.com/espressif/asio.git;protocol=https;name=asio;nobranch=1;destsuffix=esp-idf/components/asio/asio \
 git://github.com/espressif/aws-iot-device-sdk-embedded-C;protocol=https;name=aws;nobranch=1;destsuffix=esp-idf/components/aws_iot/aws-iot-device-sdk-embedded-C \
 git://github.com/espressif/esp32-bt-lib;protocol=https;name=btlib;nobranch=1;destsuffix=esp-idf/components/bt/lib \
 git://github.com/obgm/libcoap;protocol=https;name=coap;nobranch=1;destsuffix=esp-idf/components/coap/libcoap \
 git://github.com/espressif/esp32-wifi-lib;protocol=https;name=esplib;nobranch=1;destsuffix=esp-idf/components/esp32/lib \
 git://github.com/espressif/esptool;protocol=https;name=esptool;nobranch=1;destsuffix=esp-idf/components/esptool_py/esptool \
 git://github.com/libexpat/libexpat;protocol=https;name=expat;nobranch=1;destsuffix=esp-idf/components/expat/expat \
 git://github.com/DaveGamble/cJSON;protocol=https;name=cjson;nobranch=1;destsuffix=esp-idf/components/json/cJSON \
 git://github.com/jedisct1/libsodium;protocol=https;name=sodium;nobranch=1;destsuffix=esp-idf/components/libsodium/libsodium \
 git://github.com/espressif/esp-lwip;protocol=https;name=lwip;nobranch=1;destsuffix=esp-idf/components/lwip/lwip \
 git://github.com/espressif/mbedtls;protocol=https;name=tls;nobranch=1;destsuffix=esp-idf/components/mbedtls/mbedtls \
 git://github.com/kmackay/micro-ecc;protocol=https;name=ecc;nobranch=1;destsuffix=esp-idf/components/micro-ecc/micro-ecc \
 git://github.com/espressif/esp-mqtt;protocol=https;name=mqtt;nobranch=1;destsuffix=esp-idf/components/mqtt/esp-mqtt \
 git://github.com/nghttp2/nghttp2;protocol=https;name=http;nobranch=1;destsuffix=esp-idf/components/nghttp/nghttp2 \
 git://github.com/protobuf-c/protobuf-c;protocol=https;name=protobuf;nobranch=1;destsuffix=esp-idf/components/protobuf-c/protobuf-c \
 git://github.com/pellepl/spiffs.git;protocol=https;name=spiffs;nobranch=1;destsuffix=esp-idf/components/spiffs/spiffs \
 file://src \
 file://0001-lwip-enable-ip-forwarding.patch;patchdir=../../.. \
"

##############
# esp-idf v3.2
##############
SRCREV_idf      = "286202caa31b61c2182209f37f8069a0b60fb942"
SRCREV_asio     = "55efc179b76139c8f9b44bf22a4aba4803f7a7bd"
SRCREV_aws      = "299183238ffe7a3e6a5ca0af9db19c10eaca62cf"
SRCREV_btlib    = "6c3379db92017f5713e807b60208b62c205d2e05"
SRCREV_coap     = "6468887a12666f88b8704d797fc176cd4f40ee4c"
SRCREV_esplib   = "eb53491cf6eb18d6cfd98f7b3bb4e09fb241733c"
SRCREV_esptool  = "9ad444a6e06e58833d5e6044c1d5f3eb3dd56023"
SRCREV_expat    = "968b8cc46dbee47b83318d5f31a8e7907199614b"
SRCREV_cjson    = "7cc52f60356909b3dd260304c7c50c0693699353"
SRCREV_sodium   = "70170c28c844a4786e75efc626e1aeebc93caebc"
SRCREV_lwip     = "fe9a58cb7abdc74e7a2196b7c4e0a29ff9042f88"
SRCREV_tls      = "19eb57f7f7b2f4312d497ddbcb5f104fc6877c70"
SRCREV_ecc      = "d037ec89546fad14b5c4d5456c2e23a71e554966"
SRCREV_mqtt     = "f08f3b678717865234637164a29ed3a63e756ca7"
SRCREV_http     = "3bcc416e13cc790e2fb45fcfe9111d38609c5032"
SRCREV_protobuf = "dac1a65feac4ad72f612aab99f487056fbcf5c1a"
SRCREV_spiffs   = "f5e26c4e933189593a71c6b82cda381a7b21e41c"

S = "${WORKDIR}/esp-idf/components/esptool_py/esptool"

DEPENDS += "python3-pyserial-native python3-cryptography-native flex-native bison-native gperf-native"
RDEPENDS_${PN} += "python3-pyserial python3-cryptography bash"

export IDF_PATH="${WORKDIR}/esp-idf"
ESP32_TOOLCHAIN_PREFIX ?= "/opt/esp32-toolchain/xtensa-esp32-elf/bin/xtensa-esp32-elf-"

EXTRA_OEMAKE += " CONFIG_TOOLPREFIX=${ESP32_TOOLCHAIN_PREFIX}"
EXTRA_OEMAKE += " CONFIG_PYTHON=${PYTHON}"

PARALLEL_MAKE = ""

do_configure_prepend () {
  # Print rather than execute a failing python dependency check.
  sed -i -E -e 's/^([ \t]+)(.*check_python_dependencies.py)/\1echo \2/' "${WORKDIR}/esp-idf/make/project.mk"
}

do_configure() {
  cd ${WORKDIR}/esp-idf/components/esptool_py/esptool
  distutils3_do_configure
}

do_compile_append () {
  unset LDFLAGS CFLAGS EXTRA_LDFLAGS TARGET_LDFLAGS BUILD_LDFLAGS
  cd ${WORKDIR}/src
  oe_runmake all
}

do_install() {
  cd ${WORKDIR}/esp-idf/components/esptool_py/esptool
  distutils3_do_install
}

do_install_append() {
  install -d ${D}/usr/share/esp32
  cp ${WORKDIR}/src/build/wifi_softAP.bin ${D}/usr/share/esp32/
  cp ${WORKDIR}/src/build/partitions_singleapp.bin ${D}/usr/share/esp32/
  cp ${WORKDIR}/src/build/bootloader/bootloader.bin ${D}/usr/share/esp32/
  install -d ${D}/usr/sbin
  install -m 0755 ${WORKDIR}/src/flash_esp32.sh ${D}/usr/sbin/flash_esp32
  install -m 0755 ${WORKDIR}/src/make_esp32_cfg.sh ${D}/usr/sbin/make_esp32_cfg
  install -m 0755 ${WORKDIR}/esp-idf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py ${D}/usr/sbin/nvs_partition_gen.py
}

FILES_${PN} += "/usr/share/esp32 \
                /usr/share/esp32/wifi_softAP.bin \
                /usr/share/esp32/partitions_singleapp.bin \
                /usr/share/esp32/bootloader.bin"

CPPFLAGS = "${BUILD_CPPFLAGS}"
CFLAGS = "${BUILD_CFLAGS}"
CXXFLAGS = "${BUILD_CXXFLAGS}"
LDFLAGS = "${BUILD_LDFLAGS}"

# native pkg doesn't need the TOOLCHAIN_OPTIONS.
TOOLCHAIN_OPTIONS = ""

# Don't build ptest natively
PTEST_ENABLED = "0"

# Don't use site files for native builds
export CONFIG_SITE = "${COREBASE}/meta/site/native"

# Set the compiler as well. It could have been set to something else
export CC = "${BUILD_CC}"
export CXX = "${BUILD_CXX}"
export FC = "${BUILD_FC}"
export CPP = "${BUILD_CPP}"
export LD = "${BUILD_LD}"
export CCLD = "${BUILD_CCLD}"
export AR = "${BUILD_AR}"
export AS = "${BUILD_AS}"
export RANLIB = "${BUILD_RANLIB}"
export STRIP = "${BUILD_STRIP}"
export NM = "${BUILD_NM}"

inherit python3native setuptools3
