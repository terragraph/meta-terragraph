# Test to catch whether packages providing cmake config files export their $WORKDIR.
# This indicates an upstream bug with the CMake files, as a downstream package should
# not look outside its own $WORKDIR/recipe-sysroot for dependencies - and in fact will
# completely break the build if INHERIT += "rm_work" is used.
QAPATHTEST[cmake-exports-workdir] = "package_qa_check_cmake_exports_workdir"
def package_qa_check_cmake_exports_workdir(file, name, d, elf, messages):
    """
    Check for .cmake files exporting absolute paths to this package's WORKDIR
    """
    if file.endswith(".cmake"):
        import mmap
        with open(file, 'rb', 0) as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as s:
            if s.find(d.getVar('WORKDIR').encode()) != -1:
                package_qa_add_message(messages, "cmake-exports-workdir", "%s: %s contains reference to ${WORKDIR}" % (name, package_qa_clean_path(file, d, name)))

WARN_QA += "cmake-exports-workdir"

