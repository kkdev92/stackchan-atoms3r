# Checks at startup that the device is in the state it thinks it is,
# header-only. Prints the markers the emulator run looks for to decide
# whether a boot succeeded.
idf_component_register(
  INCLUDE_DIRS "include"
  REQUIRES
    stackchan_app
    stackchan_domain
    stackchan_ports
    esp_timer
    log
)
