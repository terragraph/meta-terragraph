# Pull libdb support from cyrus-sasl, not needed.

DEPENDS_remove = "virtual/db"

EXTRA_OECONF += "--with-dblib=none \
                 --with-plugindir="${libdir}/sasl2" \
                 andrew_cv_runpath_switch=none"
