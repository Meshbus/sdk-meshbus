cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_llext_contract_test)

target_sources(app PRIVATE src/test_api.c)

set(llext_targets)
set(llext_outputs)


set(ext_app_bin ${PROJECT_BINARY_DIR}/llext/mb_app.mba)
set(ext_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_app.inc)
set(ext_other_edk_app_bin ${PROJECT_BINARY_DIR}/llext/mb_otheredk.mba)
set(ext_other_edk_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_otheredk_app.inc)
set(ext_older_edk_app_bin ${PROJECT_BINARY_DIR}/llext/mb_olderedk.mba)
set(ext_older_edk_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_olderedk_app.inc)
set(ext_bad_target_app_bin ${PROJECT_BINARY_DIR}/llext/mb_badtargetapp.mba)
set(ext_bad_target_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_badtarget_app.inc)
set(ext_old_meta_app_bin ${PROJECT_BINARY_DIR}/llext/mb_oldmeta.mba)
set(ext_old_meta_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_oldmeta_app.inc)
set(ext_bad_reserved_app_bin ${PROJECT_BINARY_DIR}/llext/mb_badreserved.mba)
set(ext_bad_reserved_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_badreserved_app.inc)
set(ext_big_app_bin ${PROJECT_BINARY_DIR}/llext/mb_bigapp.mba)
set(ext_big_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_bigapp.inc)
set(ext_bad_icon_app_bin ${PROJECT_BINARY_DIR}/llext/mb_badicon.mba)
set(ext_bad_icon_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_badicon.inc)
set(ext_bad_symbol_app_bin ${PROJECT_BINARY_DIR}/llext/mb_badsymapp.mba)
set(ext_bad_symbol_app_inc ${ZEPHYR_BINARY_DIR}/include/generated/mb_badsymapp.inc)

set(MESHBUS_LLEXT_REUSE_APP "" CACHE FILEPATH
	"Existing compatible app package to embed instead of rebuilding mb_app")
if(MESHBUS_LLEXT_REUSE_APP)
	get_filename_component(ext_app_bin "${MESHBUS_LLEXT_REUSE_APP}"
		ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
	if(NOT EXISTS "${ext_app_bin}")
		message(FATAL_ERROR "Reused LLEXT app does not exist: ${ext_app_bin}")
	endif()
else()
	add_llext_target(mb_app_ext
		OUTPUT ${ext_app_bin}
		SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app_ext.c
	)
	list(APPEND llext_targets mb_app_ext)
	list(APPEND llext_outputs ${ext_app_bin})
endif()
add_llext_target(mb_otheredk_app_ext
	OUTPUT ${ext_other_edk_app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app_other_edk_ext.c
)
add_llext_target(mb_olderedk_app_ext
	OUTPUT ${ext_older_edk_app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app_older_edk_ext.c
)
add_llext_target(mb_badtarget_app_ext
	OUTPUT ${ext_bad_target_app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app_bad_target_ext.c
)
add_llext_target(mb_oldmeta_app_ext
	OUTPUT ${ext_old_meta_app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app_old_metadata_ext.c
)
add_llext_target(mb_badreserved_app_ext
	OUTPUT ${ext_bad_reserved_app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app_bad_reserved_ext.c
)
add_llext_target(mb_bigapp_ext
	OUTPUT ${ext_big_app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app_big_heap_ext.c
)
add_llext_target(mb_badicon_ext
	OUTPUT ${ext_bad_icon_app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app_bad_icon_ext.c
)
add_llext_target(mb_badsymapp_ext
	OUTPUT ${ext_bad_symbol_app_bin}
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app_bad_symbol_ext.c
)

list(APPEND llext_targets
	mb_otheredk_app_ext
	mb_olderedk_app_ext
	mb_badtarget_app_ext
	mb_oldmeta_app_ext
	mb_badreserved_app_ext
	mb_bigapp_ext
	mb_badicon_ext
	mb_badsymapp_ext
)
list(APPEND llext_outputs
	${ext_other_edk_app_bin}
	${ext_older_edk_app_bin}
	${ext_bad_target_app_bin}
	${ext_old_meta_app_bin}
	${ext_bad_reserved_app_bin}
	${ext_big_app_bin}
	${ext_bad_icon_app_bin}
	${ext_bad_symbol_app_bin}
)

generate_inc_file_for_target(app ${ext_app_bin} ${ext_app_inc})
generate_inc_file_for_target(app ${ext_other_edk_app_bin} ${ext_other_edk_app_inc})
generate_inc_file_for_target(app ${ext_older_edk_app_bin} ${ext_older_edk_app_inc})
generate_inc_file_for_target(app ${ext_bad_target_app_bin} ${ext_bad_target_app_inc})
generate_inc_file_for_target(app ${ext_old_meta_app_bin} ${ext_old_meta_app_inc})
generate_inc_file_for_target(app ${ext_bad_reserved_app_bin} ${ext_bad_reserved_app_inc})
generate_inc_file_for_target(app ${ext_big_app_bin} ${ext_big_app_inc})
generate_inc_file_for_target(app ${ext_bad_icon_app_bin} ${ext_bad_icon_app_inc})
generate_inc_file_for_target(app ${ext_bad_symbol_app_bin} ${ext_bad_symbol_app_inc})

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
