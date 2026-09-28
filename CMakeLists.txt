zephyr_include_directories(include)
add_subdirectory(bootloader)
add_subdirectory(modules)
add_subdirectory(drivers)
add_subdirectory(subsys)

if(TARGET drivers__charger)
	target_sources(drivers__charger PRIVATE ${CMAKE_CURRENT_LIST_DIR}/cmake/empty.c)
endif()

if(TARGET drivers__mfd)
	target_sources(drivers__mfd PRIVATE ${CMAKE_CURRENT_LIST_DIR}/cmake/empty.c)
endif()

if(TARGET drivers__led)
	target_sources(drivers__led PRIVATE ${CMAKE_CURRENT_LIST_DIR}/cmake/empty.c)
endif()

if(TARGET drivers__display)
	target_sources(drivers__display PRIVATE ${CMAKE_CURRENT_LIST_DIR}/cmake/empty.c)
endif()
