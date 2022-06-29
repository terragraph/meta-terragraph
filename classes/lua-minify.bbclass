DEPENDS_prepend = "lua-luaminify-native "

do_install_append () {
    for f in `find ${D}/ -name '*.lua'`; do
        lua-minify "${f}"
    done
}
