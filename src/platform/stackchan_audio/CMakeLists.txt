# Sound on the Atomic Voice Base: an ES8311 codec over I2S.
#
# Implements ports::AudioSource and ports::AudioSink. The part number does
# not escape this component. This adapter uses half-duplex operation;
# domain::AudioDirection defines the tested transition table.
# The board supplies BCLK without a separate MCLK, so this component configures
# the codec registers for that clock arrangement.
idf_component_register(
  SRCS "src/voice_base.cpp"
  INCLUDE_DIRS "include"
  REQUIRES stackchan_ports stackchan_domain
  PRIV_REQUIRES esp_driver_i2s esp_driver_i2c log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror=return-type)
