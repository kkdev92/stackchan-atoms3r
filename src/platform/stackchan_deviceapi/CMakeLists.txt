# The HTTP surface. Carrying only: every decision it appears to make is
# actually made in core, by stackchan_app or stackchan_domain.
#
# One file per route. To add a route: add the file, then one line to SRCS and
# one to the install list in server.cpp.
idf_component_register(
  SRCS
    "src/server.cpp"
    "src/responses.cpp"
    "src/request_origin.cpp"
    "src/command_route.cpp"
    "src/provisioning_routes.cpp"
    "src/info_route.cpp"
  INCLUDE_DIRS "include"
  REQUIRES
    stackchan_app
    stackchan_domain
  PRIV_REQUIRES
    stackchan_net
    stackchan_runtime
    esp_http_server
    esp_netif
    lwip
    log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror=return-type)
