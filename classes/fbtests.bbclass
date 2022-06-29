# Create a separate package for /usr/sbin/tests unit tests.
# Only pull them into the image if building the tgx86 emulator.

# Gather up the tests into a ${PN}-tests package.
PACKAGES =+ "${PN}-tests"
FILES_${PN}-tests += "/usr/sbin/tests"
RDEPENDS_${PN}-tests += "${PN}"

# Allow the tests and main package to be empty if there
# are no tests or no non-tests.
ALLOW_EMPTY_${PN} = "1"
ALLOW_EMPTY_${PN}-tests = "1"

# Pull the tests into the emulation build.
RDEPENDS_${PN}_append_tgx86 = " ${PN}-tests"
