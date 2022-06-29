# Build without sensord to save space.
PACKAGECONFIG_remove = "sensord"

# lmsensors incorrectly depends upon sensord even when configured not to use it.
RDEPENDS_${PN}_remove = "lmsensors-sensord"
DEPENDS_remove = "rrdtool"
