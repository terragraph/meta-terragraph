FILESEXTRAPATHS_prepend := "${THISDIR}/thrift:"

SRC_URI += "file://no-module-keyword.patch \
            file://fix-lua-generator-empty-typedef.patch \
            file://lua-struct-reader-use-fname.patch \
            file://fix-lua-generator-default-service-args.patch \
"
