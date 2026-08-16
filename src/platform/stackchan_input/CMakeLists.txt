# The button, as far as the wiring goes. Debouncing it and deciding what a
# given sequence of presses means are done in core, where both are tested on
# a host.
idf_component_register(
  SRCS "src/button.cpp"
  INCLUDE_DIRS "include"
  REQUIRES
    stackchan_domain
  PRIV_REQUIRES
    esp_driver_gpio
    log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror=return-type)
