if(CONFIG_ZMK_DISPLAY AND CONFIG_NICE_VIEW_WIDGET_STATUS)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(custom_status_screen.c)

  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(widgets/screen.c)
    zephyr_library_sources(widgets/util.c)

    # Display callbacks share a 2 KiB workqueue stack with the renderer/LVGL.
    # In particular, never pass the large memory snapshot through a by-value
    # ZMK_DISPLAY_WIDGET_LISTENER state. Check the actual optimized target code.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/widgets/screen.c
        TARGET_DIRECTORY zephyr APPEND PROPERTY COMPILE_OPTIONS
        -Werror=stack-usage=256)
    endif()
  endif()
endif()
