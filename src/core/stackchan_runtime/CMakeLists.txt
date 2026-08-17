# The concurrency tools: the abstractions, plus the parts that need no
# operating system underneath them.
#
# REQUIRES is empty. Nothing here calls FreeRTOS, which is what makes it
# possible to test on a PC that a deadline expires when it should and that a
# cancellation is always delivered. The parts that genuinely have to wait on
# the OS live in platform/ instead.
idf_component_register(
  SRCS "src/deadline.cpp" "src/cancellation.cpp"
  INCLUDE_DIRS "include"
)
