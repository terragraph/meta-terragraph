
# Compute the terragraph VERSION string
def tg_find_version(d):
    import subprocess
    class NoGitDir(Exception):
        pass

    revision = "unknown"
    srcdir = d.getVar("META_TERRAGRAPH_DIR")
    vfile = os.path.join(srcdir, "VERSION")
    user = d.getVar("USER")
    try:
        if not os.path.isdir(os.path.join(srcdir, ".git")):
            raise NoGitDir
        if user:
            cmd = "git describe --dirty=\"-%s\" --long" % user
        else:
            cmd = "git describe --long"
        revision = subprocess.check_output(cmd, cwd=srcdir, shell=True).rstrip().decode('utf-8')
    except (NoGitDir, subprocess.CalledProcessError):
        if os.path.exists(vfile):
            with open(vfile, "r") as v:
                revision = v.readline().strip() + "-versionfile"
            if user:
                revision += "-" + user
    return revision

# Pull the release number out of the VERSION variable.
def tg_version_to_version_id(version):
    import re
    p = re.compile(r"[^0-9]+([0-9_]*[0-9])")
    m = p.match(version)
    return m.group(1).replace("_", ".")

def hostname_short():
    import socket
    return socket.gethostname().split('.')[0]

require os-release-utils.inc

# Always force a reparse so that these variables are recomputed if they change.
BB_DONT_CACHE = "1"
python() {
    ver = tg_find_version(d)
    d.setVar("VERSION", ver)
    d.setVar("VERSION_ID", tg_version_to_version_id(ver))
    d.setVar("OSRELEASE_EMAIL", d.getVar("USER") + "@" + hostname_short())
}

ID = "terragraph"
ID_LIKE = "poky"
NAME = "${VENDOR_ID}Terragraph"
BUILD_ID = "${VERSION} ${DATETIME_ISOFORMAT}"
PRETTY_NAME = "${NAME} Release ${VERSION} ${OSRELEASE_EMAIL} ${DATETIME_ISOFORMAT}"
HOME_URL = "https://terragraph.com/"

OS_RELEASE_FIELDS = "ID ID_LIKE NAME VERSION VERSION_ID BUILD_ID PRETTY_NAME HOME_URL"

# Build /etc/tgversion for backwards compatibility
python do_compile_append() {
    with open(os.path.join(d.getVar("B"), "tgversion"), "w") as f:
        print(d.getVar("PRETTY_NAME"), file=f)
}

do_install_append() {
    install -m 0644 tgversion ${D}/${sysconfdir}/tgversion
}
