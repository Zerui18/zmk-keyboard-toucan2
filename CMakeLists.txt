zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/src)

# Linked worktrees can point outside Docker's /repo bind mount, so prefer the
# revision resolved by the host build wrapper and fall back to an in-container
# lookup for ordinary standalone checkouts.
set(TOUCAN_GIT_SHA "$ENV{TOUCAN_GIT_SHA}")
if(NOT TOUCAN_GIT_SHA)
  execute_process(
    COMMAND git -C ${CMAKE_CURRENT_LIST_DIR} rev-parse --short=7 HEAD
    OUTPUT_VARIABLE TOUCAN_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()
if(NOT TOUCAN_GIT_SHA)
  set(TOUCAN_GIT_SHA UNKNOWN)
endif()
string(TOUPPER "${TOUCAN_GIT_SHA}" TOUCAN_GIT_SHA)
zephyr_compile_definitions(
  TOUCAN_FIRMWARE_VERSION="0.3"
  TOUCAN_GIT_SHA="${TOUCAN_GIT_SHA}"
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

target_sources_ifdef(CONFIG_TOUCAN_MEMORY app PRIVATE
  src/toucan_memory.c
)

target_sources_ifdef(CONFIG_TOUCAN_SOFT_POWER app PRIVATE
  src/toucan_soft_power.c
)

target_sources_ifdef(CONFIG_TOUCAN_BOOTLOADER app PRIVATE
  src/toucan_bootloader.c
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
