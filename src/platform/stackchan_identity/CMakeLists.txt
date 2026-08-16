# Collects what makes this particular board itself.
#
# Reading the MAC address out of eFuse and asking the hardware for random
# numbers both need ESP-IDF, so this cannot live in core. It gathers the
# values and hands them to app::DeviceIdentity; what is made of them is
# decided there.
idf_component_register(
  SRCS "src/device.cpp" "src/token.cpp"
  INCLUDE_DIRS "include"
  REQUIRES stackchan_app
  PRIV_REQUIRES esp_hw_support esp_app_format esp_timer bootloader_support nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror=return-type)
