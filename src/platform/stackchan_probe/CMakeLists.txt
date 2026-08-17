# Finds out what is actually attached, by seeing which I2C addresses answer.
#
# This is what lets capabilities describe the board in front of you rather
# than the board that was assumed. Assume instead, and a unit with no voice
# base accepts a request to record and then fails part-way through it.
idf_component_register(
  SRCS "src/hardware.cpp"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES esp_driver_i2c log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror=return-type)
