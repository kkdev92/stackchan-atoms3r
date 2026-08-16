# Wi-Fi: joining a network, and raising an access point when there is none
# to join.
#
# How long to wait before trying again, and when to give up and open the
# access point, are decided by domain::NetworkPolicy. None of that needs a
# radio, so it is tested on a host instead of by watching a board reconnect.
idf_component_register(
  SRCS "src/wifi.cpp"
  INCLUDE_DIRS "include"
  REQUIRES stackchan_domain
  PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_hw_support bootloader_support log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror=return-type)
