target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/check_profiles.c)

# Native POSIX has no Bluetooth controller. Exercise the real platform module's
# BLE branches with five profiles, stubbing only the endpoint/profile getters.
# The rest of ZMK remains a non-BLE build; real endpoint events and settings run.
get_filename_component(PLATFORM_SOURCE
  ${CMAKE_CURRENT_LIST_DIR}/../../src/toucan_platform_mode.c ABSOLUTE)
set_property(SOURCE ${PLATFORM_SOURCE} ${CMAKE_CURRENT_LIST_DIR}/check_profiles.c
  TARGET_DIRECTORY app APPEND PROPERTY COMPILE_DEFINITIONS
  CONFIG_ZMK_BLE=1 CONFIG_BT_MAX_PAIRED=5)
zephyr_ld_options(-Wl,--wrap=zmk_endpoints_selected)
