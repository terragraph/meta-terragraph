SUMMARY = "Packages to include in the x86 image for testing"

inherit packagegroup

# ptr installs python3-venv + deps for driving Python testing utils

RDEPENDS_${PN} = "\
    ptr \
"
