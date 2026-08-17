# The use cases: what the device does, expressed as steps.
#
# Depends on the abstractions in ports and nothing else. It has no idea which
# implementation under platform/ will be handed to it.
idf_component_register(
  SRCS "src/command_registry.cpp" "src/envelope.cpp" "src/device_describe.cpp" "src/conversation.cpp" "src/request_router.cpp" "src/playback_stager.cpp" "src/scan_params.cpp" "src/health_payload.cpp"
  INCLUDE_DIRS "include"
  REQUIRES stackchan_domain stackchan_ports
)
