# zephyr-keep-sorted-start

add_subdirectory_ifdef(CONFIG_IMG_MANAGER dfu)
add_subdirectory_ifdef(CONFIG_MESHBUS meshbus)
add_subdirectory_ifdef(CONFIG_U8G2 u8g2)
add_subdirectory_ifdef(CONFIG_ZUI zui)

# zephyr-keep-sorted-stop
