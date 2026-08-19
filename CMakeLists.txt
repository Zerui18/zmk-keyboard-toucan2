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

target_sources_ifdef(CONFIG_TOUCAN_SPLIT_XY_PACKING app PRIVATE
  src/toucan_split_xy_packing.c
)
