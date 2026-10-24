# needed for languages.cmake to correctly generate CPack components
set(PLUGIN_NAME ${VACUUM_LOADER_NAME})
set(PLUGIN_DISPLAY_NAME "loader")

include("${CMAKE_SOURCE_DIR}/src/make/config.cmake")
include("${CMAKE_SOURCE_DIR}/src/translations/languages.cmake") 
include("loader.cmake")

project(loader CXX)

include_directories("${CMAKE_BINARY_DIR}/src/loader" "${CMAKE_SOURCE_DIR}/src")

if (WIN32)
	if (MINGW)
		add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/loader.rc.obj"
			COMMAND windres.exe
				"-I${CMAKE_CURRENT_SOURCE_DIR}"
				"-i${CMAKE_CURRENT_SOURCE_DIR}/loader.rc"
				-o "${CMAKE_CURRENT_BINARY_DIR}/loader.rc.obj")
		set(SOURCES ${SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/loader.rc.obj")
	else (MINGW)
		set(SOURCES ${SOURCES} "loader.rc")
	endif (MINGW)
endif (WIN32)

add_translations(TRANSLATIONS "${VACUUM_LOADER_NAME}" ${SOURCES} ${UIS})
add_executable(${VACUUM_LOADER_NAME} WIN32 ${SOURCES} ${HEADERS} ${UIS} ${TRANSLATIONS})
set_target_properties(${VACUUM_LOADER_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Qt6 setup
target_link_libraries(${VACUUM_LOADER_NAME} PRIVATE
	${VACUUM_UTILS_NAME}
	Qt6::Core
	Qt6::Gui
	Qt6::Widgets
	Qt6::Xml
)

install(TARGETS ${VACUUM_LOADER_NAME}
	RUNTIME	DESTINATION "${INSTALL_BINS}"
	COMPONENT ${VACUUM_LOADER_NAME})

if (OS2)
	# These Qt plugins are loaded at runtime and are not part of the Qt DLLs.
	# Resolve them from the same Qt prefix as Qt6Gui, fail early if the target
	# installation is incomplete, and install them under Qt's plugin categories.
	get_filename_component(_vacuum_qt_prefix "${Qt6Gui_DIR}/../../.." ABSOLUTE)
	set(_vacuum_qos2_plugin "${_vacuum_qt_prefix}/plugins/platforms/qos2.dll")
	set(_vacuum_qsqlite_plugin "${_vacuum_qt_prefix}/plugins/sqldrivers/qsqlite.dll")
	foreach(_vacuum_qt_plugin IN ITEMS "${_vacuum_qos2_plugin}" "${_vacuum_qsqlite_plugin}")
		if (NOT EXISTS "${_vacuum_qt_plugin}")
			message(FATAL_ERROR "Missing required OS/2 Qt runtime plugin: ${_vacuum_qt_plugin}")
		endif()
	endforeach()
	install(FILES "${_vacuum_qos2_plugin}"
		DESTINATION "${INSTALL_PLUGINS}/platforms")
	install(FILES "${_vacuum_qsqlite_plugin}"
		DESTINATION "${INSTALL_PLUGINS}/sqldrivers")
	# Install the Qt SerialPort runtime DLL used by Vacuum plugins.
	# Resolve it from the same Qt prefix and fail early if it is unavailable.
	get_filename_component(_vacuum_qt_dll_dir "${_vacuum_qt_prefix}/bin" ABSOLUTE)
	set(_vacuum_qtsp_dll "${_vacuum_qt_dll_dir}/Qt6SP.dll")
	if (NOT EXISTS "${_vacuum_qtsp_dll}")
		message(FATAL_ERROR "Missing required OS/2 Qt runtime library: ${_vacuum_qtsp_dll}")
	endif()
	install(FILES "${_vacuum_qtsp_dll}"
		DESTINATION "${INSTALL_BINS}")
	install(FILES "${CMAKE_SOURCE_DIR}/src/packages/os2/qt.conf"
		DESTINATION "${INSTALL_BINS}")
endif()

if (UNIX AND NOT APPLE AND NOT HAIKU)
	install(FILES "${CMAKE_SOURCE_DIR}/resources/menuicons/shared/vacuum.png"
		DESTINATION "${INSTALL_RES_DIR}/pixmaps")
	install(FILES "${CMAKE_SOURCE_DIR}/src/packages/linux/vacuum.desktop"
		DESTINATION "${INSTALL_RES_DIR}/applications")
endif (UNIX AND NOT APPLE AND NOT HAIKU)

cpack_add_component(${VACUUM_LOADER_NAME}
	DISPLAY_NAME "Loader"
	REQUIRED
	GROUP core)
