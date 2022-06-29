SUMMARY = "Fast Log processor and Forwarder"
DESCRIPTION = "Fluent Bit is a data collector, processor and  \
forwarder for Linux. It supports several input sources and \
backends (destinations) for your data. \
"

HOMEPAGE = "http://fluentbit.io"
BUGTRACKER = "https://github.com/fluent/fluent-bit/issues"

LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=2ee41112a44fe7014dce33e26468ba93"
SECTION = "net"

SRCREV = "67b144340b46bbc2585363cc1fb2de6b662e7b36"
SRC_URI = "git://github.com/fluent/fluent-bit.git;protocol=https;nobranch=1 \
           file://fix-mbedtls-cmake.patch \
           file://reduce-engine-logging.patch \
           "

S = "${WORKDIR}/git"

DEPENDS = "zlib bison-native flex-native update-rc.d-native openssl"

FILES_${PN}-dev += "${base_libdir}/systemd/"

# Use CMake 'Unix Makefiles' generator
OECMAKE_GENERATOR ?= "Unix Makefiles"

# Fluent Bit build options
# ========================

# Host related setup
EXTRA_OECMAKE += "-DGNU_HOST=${HOST_SYS}"
EXTRA_OECMAKE += "-DHOST=${TARGET_SYS}"

# Disable LuaJIT and filter_lua support
EXTRA_OECMAKE += "-DFLB_LUAJIT=Off -DFLB_FILTER_LUA=Off "

# Disable unused input plugins
EXTRA_OECMAKE += "-DFLB_IN_COLLECTD=Off "
EXTRA_OECMAKE += "-DFLB_IN_DOCKER=Off "
EXTRA_OECMAKE += "-DFLB_IN_DOCKER_EVENTS=Off "
EXTRA_OECMAKE += "-DFLB_IN_HEALTH=Off "
EXTRA_OECMAKE += "-DFLB_IN_MQTT=Off "
EXTRA_OECMAKE += "-DFLB_IN_OPENTELEMETRY=Off "
EXTRA_OECMAKE += "-DFLB_IN_THERMAL=Off "

# Disable unused filter plugins
EXTRA_OECMAKE += "-DFLB_FILTER_AWS=Off "
EXTRA_OECMAKE += "-DFLB_FILTER_GEOIP2=Off "
EXTRA_OECMAKE += "-DFLB_FILTER_NIGHTFALL=Off "

# Disable unused output plugins
EXTRA_OECMAKE += "-DFLB_OUT_AZURE=Off "
EXTRA_OECMAKE += "-DFLB_OUT_AZURE_BLOB=Off "
EXTRA_OECMAKE += "-DFLB_OUT_BIGQUERY=Off "
EXTRA_OECMAKE += "-DFLB_OUT_CLOUDWATCH_LOGS=Off "
EXTRA_OECMAKE += "-DFLB_OUT_DATADOG=Off "
EXTRA_OECMAKE += "-DFLB_OUT_KINESIS_FIREHOSE=Off "
EXTRA_OECMAKE += "-DFLB_OUT_KINESIS_STREAMS=Off "
EXTRA_OECMAKE += "-DFLB_OUT_OPENSEARCH=Off "
EXTRA_OECMAKE += "-DFLB_OUT_OPENTELEMETRY=Off "
EXTRA_OECMAKE += "-DFLB_OUT_GELF=Off "
EXTRA_OECMAKE += "-DFLB_OUT_INFLUXDB=Off "
EXTRA_OECMAKE += "-DFLB_OUT_LOGDNA=Off "
EXTRA_OECMAKE += "-DFLB_OUT_LOKI=Off "
EXTRA_OECMAKE += "-DFLB_OUT_NATS=Off "
EXTRA_OECMAKE += "-DFLB_OUT_NRLOGS=Off "
EXTRA_OECMAKE += "-DFLB_OUT_PGSQL=Off "
EXTRA_OECMAKE += "-DFLB_OUT_SKYWALKING=Off "
EXTRA_OECMAKE += "-DFLB_OUT_SLACK=Off "
EXTRA_OECMAKE += "-DFLB_OUT_S3=Off "
EXTRA_OECMAKE += "-DFLB_OUT_SPLUNK=Off "
EXTRA_OECMAKE += "-DFLB_OUT_STACKDRIVER=Off "
EXTRA_OECMAKE += "-DFLB_OUT_TD=Off "

# Disable Library and examples
EXTRA_OECMAKE += "-DFLB_SHARED_LIB=Off -DFLB_EXAMPLES=Off "

# Enable TLS
EXTRA_OECMAKE += "-DFLB_TLS=On "

# Kafka Output plugin (disabled by default)
# EXTRA_OECMAKE += "-DFLB_OUT_KAFKA=On "

inherit cmake

TARGET_CC_ARCH_append = " ${SELECTED_OPTIMIZATION}"
