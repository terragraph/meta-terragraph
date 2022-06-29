require recipes-core/images/terragraph-image.bb

PACKAGE_EXCLUDE = "gdb"

ROOTFS_POSTPROCESS_COMMAND_append += "terragraph_rootfs_delete_files; "

terragraph_rootfs_delete_files() {
  # Delete non-.pyc python files
  # This finds all .pyc files (one per line), triples every line,
  # then processes lines in batches of 3. For every batch makes
  # a substitution to generate a filename to be deleted (opt-? and .py).
  # If a generated file doesn't exist, rm won't complain due to -f.
  find "$(echo ${IMAGE_ROOTFS}/usr/lib/python?.?)" -name '*.cpython-??.pyc' |
    sed -n 'p;p;p' |
    sed \
      -e 's|\.pyc$|.opt-1.pyc| ; n' \
      -e 's|\.pyc$|.opt-2.pyc| ; n' \
      -e 's|\.cpython-..\.pyc$|.py| ; s|__pycache__/||' |
    xargs rm -f
}
