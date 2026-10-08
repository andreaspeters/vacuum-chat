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
