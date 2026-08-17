# The layer that decides what is correct.
#
# REQUIRES is empty, and stays empty: nothing here depends on any part of
# ESP-IDF. That is what lets every rule in this component be tested on a PC
# in a second, and needing to add a dependency here is a sign the decision
# has been put in the wrong layer.
idf_component_register(
  SRCS "src/button.cpp" "src/health.cpp" "src/protocol.cpp" "src/ids.cpp" "src/json_writer.cpp" "src/expression.cpp" "src/network_policy.cpp" "src/device_state.cpp" "src/access_token.cpp" "src/sse.cpp" "src/speech.cpp" "src/base64.cpp" "src/json_scan.cpp" "src/wav.cpp" "src/button_gesture.cpp" "src/audio_direction.cpp" "src/provisioning_scope.cpp"
  INCLUDE_DIRS "include"
)
