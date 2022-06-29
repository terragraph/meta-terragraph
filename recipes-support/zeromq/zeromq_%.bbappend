FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += "file://0001-autoconf-enable-tcp-keepvalive-cross-compile.patch"

# Enable TCP keepalives in CMake while cross-compiling
EXTRA_OECMAKE_append = " -DZMQ_HAVE_SOCK_CLOEXEC=1"
EXTRA_OECMAKE_append = " -DZMQ_HAVE_EVENTFD_CLOEXEC=1"
EXTRA_OECMAKE_append = " -DZMQ_HAVE_SO_KEEPALIVE=1"
EXTRA_OECMAKE_append = " -DZMQ_HAVE_TCP_KEEPCNT=1"
EXTRA_OECMAKE_append = " -DZMQ_HAVE_TCP_KEEPIDLE=1"
EXTRA_OECMAKE_append = " -DZMQ_HAVE_TCP_KEEPINTVL=1"
EXTRA_OECMAKE_append = " -DZMQ_HAVE_TCP_KEEPALIVE=1"
EXTRA_OECMAKE_append = " -DZMQ_HAVE_TCP_KEEPINTVL=1"
