# The main component. It wires things together and does nothing else.
#
# REQUIRES lists both core and platform components because this is the one
# place allowed to know an abstraction and its implementation at the same
# time. Everywhere else knows only one of the two, which is what keeps the
# dependencies pointing in a single direction.
idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES
    stackchan_app
    stackchan_domain
    stackchan_ports
    stackchan_runtime
    stackchan_board
    stackchan_identity
    stackchan_net
    stackchan_probe
    stackchan_display
    stackchan_input
    stackchan_audio
    stackchan_conversation
    stackchan_commands
    stackchan_selftest
    stackchan_deviceapi
    stackchan_ui
    esp_psram
    esp_timer
    freertos
    heap
    log
)

# Additional warnings apply to this project's own code.
target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wextra
  -Werror=return-type
)
