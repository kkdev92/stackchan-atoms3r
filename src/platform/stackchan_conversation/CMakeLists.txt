# A conversation: record, post to the gateway, read the event stream back,
# play what comes out of it.
#
# Reading the stream is app::ConversationTurn's job, in core, where it is
# tested against every way a stream can be split. What is left here is the
# carrying: an HTTP client, a task to run it on, and keeping the half-duplex
# codec pointed the right way.
idf_component_register(
  SRCS "src/gateway_client.cpp" "src/task.cpp"
  INCLUDE_DIRS "include"
  REQUIRES stackchan_app stackchan_domain stackchan_ports stackchan_runtime stackchan_identity
  PRIV_REQUIRES esp_http_client nvs_flash esp_timer heap log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror=return-type)
