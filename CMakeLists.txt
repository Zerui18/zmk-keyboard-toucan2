zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/src)

target_sources_ifdef(CONFIG_TOUCAN_SOFT_POWER app PRIVATE
  src/toucan_soft_power.c
)

target_sources_ifdef(CONFIG_TOUCAN_BATTERY_ESTIMATOR app PRIVATE
  src/toucan_battery_estimator.c
)

target_sources_ifdef(CONFIG_TOUCAN_BATTERY_READOUT app PRIVATE
  src/behavior_toucan_battery_readout.c
  src/toucan_battery_readout.c
)

target_sources_ifdef(CONFIG_TOUCAN_PLATFORM_MODE app PRIVATE
  src/toucan_platform_mode.c
)

target_sources_ifdef(CONFIG_TOUCAN_USB_BOOTLOADER_TOUCH app PRIVATE
  src/toucan_usb_bootloader_touch.c
)

target_sources_ifdef(CONFIG_TOUCAN_SPLIT_XY_PACKING app PRIVATE
  src/toucan_split_xy_packing.c
)

target_sources_ifdef(CONFIG_TOUCAN_SPLIT_STATUS app PRIVATE
  src/toucan_split_status.c
)
