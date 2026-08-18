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
