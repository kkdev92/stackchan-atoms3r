# What the screen shows and what a press leads to.
#
# It reaches into a lot of components, because presenting the device's state
# means knowing that state. What it does not do is decide any of it: what a
# press means comes from core, and conversations, networking and probing are
# each carried out by their own component. This one only arranges them.
idf_component_register(
  SRCS "src/presenter.cpp"
  INCLUDE_DIRS "include"
  REQUIRES
    stackchan_ports
    stackchan_domain
    stackchan_runtime
    stackchan_app
    stackchan_identity
    stackchan_audio
    stackchan_net
    stackchan_conversation
    stackchan_probe
  PRIV_REQUIRES
    log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror=return-type)
