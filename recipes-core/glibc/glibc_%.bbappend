# Allow bitbake to find the file
FILESEXTRAPATHS_append := ":${FILE_DIRNAME}/files"

SRC_URI += "file://strip_override"

do_install_append() {
    chmod a+x ${WORKDIR}/strip_override
}

# Remember real strip command
TG_REAL_STRIP := "${STRIP}"
export TG_REAL_STRIP

# Override strip command with our hack
STRIP_class-target = "${WORKDIR}/strip_override"
