cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_llext_service_dut_test)

target_sources(app PRIVATE src/test_service_dut.c)
target_include_directories(app PRIVATE ../src)

set(llext_targets)
set(llext_outputs)


set(app_bin ${PROJECT_BINARY_DIR}/llext/mb_app.mba)
set(app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_app.inc)
set(bad_symbol_app_bin ${PROJECT_BINARY_DIR}/llext/mb_badsymapp.mba)
set(bad_symbol_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_badsymapp.inc)

add_llext_target(mb_app_ext
	OUTPUT ${app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/app_ext.c
)
add_llext_target(mb_badsymapp_ext
	OUTPUT ${bad_symbol_app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/app_bad_symbol_ext.c
)

list(APPEND llext_targets mb_app_ext mb_badsymapp_ext)
list(APPEND llext_outputs ${app_bin} ${bad_symbol_app_bin})

generate_inc_file_for_target(app ${app_bin} ${app_inc})
generate_inc_file_for_target(app ${bad_symbol_app_bin} ${bad_symbol_app_inc})

foreach(llext_target ${llext_targets})
	if(TARGET ${llext_target}_llext_lib AND TARGET meshbus_proto)
		add_dependencies(${llext_target}_llext_lib meshbus_proto)
	endif()
endforeach()

foreach(ext ${llext_outputs})
	add_custom_command(
		OUTPUT ${ext}
		APPEND
		COMMAND ${CMAKE_OBJCOPY}
			--set-section-flags
			.meshbus.llext.meta=contents,readonly
			${ext}
	)
endforeach()
