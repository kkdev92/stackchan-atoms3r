# The command families, header-only, one file per family.
#
# To add a family: add the file, then add one line to register_commands.hpp.
# No SRCS, so this is an interface component and only propagates includes.
idf_component_register(
  INCLUDE_DIRS "include"
  REQUIRES
    stackchan_app
    stackchan_domain
    stackchan_ports
    stackchan_runtime
    stackchan_identity
    stackchan_net
    stackchan_conversation
    heap
    log
)
