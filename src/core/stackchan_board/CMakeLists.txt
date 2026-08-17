# Facts about the AtomS3R, as data. Touches no hardware.
#
# The one place pin numbers are written down. Spread across the drivers
# instead, they disagree with each other the first time a board revision
# differs. Code that actually drives a pin belongs under platform/; this
# component only answers questions.
idf_component_register(
  SRCS "src/profile.cpp"
  INCLUDE_DIRS "include"
)
