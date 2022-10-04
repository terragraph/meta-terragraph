do_install_append() {
    sed -i '1 a \
\
# Disable serial console via envParams.SERIAL_CONSOLE_DISABLE\
while [ -f "/tmp/config_serial_console_disable" ]; do sleep 10; done\
' ${D}${base_bindir}/start_getty
}
