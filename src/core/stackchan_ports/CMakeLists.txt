# The boundary with the outside world: abstractions only. Every
# implementation of them lives under platform/.
#
# No SRCS, because there is nothing but headers. ESP-IDF turns a component
# with no sources into an INTERFACE library, which is what is wanted here.
idf_component_register(
  INCLUDE_DIRS "include"
  REQUIRES stackchan_domain stackchan_runtime
)
