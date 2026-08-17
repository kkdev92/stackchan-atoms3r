# Where implementations of ports::Face live, one per file.
#
#   m5gfx_face   the face on real hardware, drawn with M5GFX. Reads the
#                panel ID to tell a GC9107 from an ST7735
#
# With no display present — under the emulator, or on a bare board — begin()
# returns false, and face.set_expression then reports itself in capabilities
# as available:false with the reason display_not_detected. Nothing pretends
# to have drawn something it did not.
#
# To show the face some other way, add a file here implementing the same
# ports::Face.
idf_component_register(
  SRCS "src/m5gfx_face.cpp"
  INCLUDE_DIRS "include"
  REQUIRES stackchan_ports stackchan_domain
  PRIV_REQUIRES m5stack__m5gfx log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror=return-type)
