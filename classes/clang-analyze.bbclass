# Replace CC and CXX with a wrapper that runs 'clang --analyze' first
# and then invokes the proper CC and CXX if the analyzer succeedes.
#
# The wrappers are currently in meta-terragraph/utils and expect to find
# clang in the path.  Because this uses the native clang compiler it
# only works when the host and native architectures match, IE it can
# only build meta-x86 targets.  Trying to compile arm or similar will
# cause clang to barf out on the architecture settings.

export CLANG_ANALYZE_REAL_CC
export CLANG_ANALYZE_REAL_CXX

python() {
    if d.getVar('ENABLE_CLANG_ANALYZE', True):
        d.setVar('CLANG_ANALYZE_REAL_CC', d.getVar('CC', False))
        d.setVar('CLANG_ANALYZE_REAL_CXX', d.getVar('CXX', False))
        d.setVar('OECMAKE_C_COMPILER', '${COREBASE}/../../utils/clang-analyze-wrapper')
        d.setVar('OECMAKE_CXX_COMPILER', '${COREBASE}/../../utils/clang-analyze-wrapper++')
}
