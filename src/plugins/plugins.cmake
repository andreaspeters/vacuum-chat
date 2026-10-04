if (NOT DEFINED PLUGIN_DISPLAY_NAME)
	set(PLUGIN_DISPLAY_NAME ${PLUGIN_NAME})
endif (NOT DEFINED PLUGIN_DISPLAY_NAME)
if (NOT DEFINED PLUGIN_DESCRIPTION)
	set(PLUGIN_DESCRIPTION "${PLUGIN_DISPLAY_NAME}")
endif (NOT DEFINED PLUGIN_DESCRIPTION)

include("${CMAKE_SOURCE_DIR}/src/make/config.cmake")
include("${CMAKE_SOURCE_DIR}/src/translations/languages.cmake") 

set(PLUGIN_${PLUGIN_NAME} YES CACHE BOOL "Enable ${PLUGIN_NAME} plugin")

set(IS_ENABLED ${PLUGIN_${PLUGIN_NAME}})

if (IS_ENABLED)
	include_directories("${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_BINARY_DIR}/src/plugins/${PLUGIN_NAME}"
		".")
	add_definitions(-DQT_PLUGIN -DQT_SHARED)

	add_translations(TRANSLATIONS ${PLUGIN_NAME} ${HEADERS} ${SOURCES} ${UIS})
	add_library(${PLUGIN_NAME} SHARED ${SOURCES} ${HEADERS} ${UIS} ${TRANSLATIONS})
	target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
	if (OS2)
		# Keep the logical target name for dependencies, but use the qmake
		# TARGET_SHORT value for the OS/2 8.3 library filename.
		set(OS2_PROJECT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_NAME}.pro")
		set(OS2_TARGET_SHORT "")
		if (EXISTS "${OS2_PROJECT_FILE}")
			file(STRINGS "${OS2_PROJECT_FILE}" OS2_TARGET_SHORT
				REGEX "^os2:[ 	]*TARGET_SHORT[ 	]*=")
		endif (EXISTS "${OS2_PROJECT_FILE}")
		if (OS2_TARGET_SHORT)
			string(REGEX REPLACE "^os2:[ 	]*TARGET_SHORT[ 	]*=[ 	]*" "" OS2_TARGET_SHORT "${OS2_TARGET_SHORT}")
		else (OS2_TARGET_SHORT)
			foreach(OS2_PLUGIN_OUTPUT_NAME ${OS2_PLUGIN_OUTPUT_NAMES})
				string(REGEX MATCH "^${PLUGIN_NAME}=(.*)$" OS2_PLUGIN_OUTPUT_MATCH "${OS2_PLUGIN_OUTPUT_NAME}")
				if (OS2_PLUGIN_OUTPUT_MATCH)
					set(OS2_TARGET_SHORT "${CMAKE_MATCH_1}")
				endif (OS2_PLUGIN_OUTPUT_MATCH)
			endforeach(OS2_PLUGIN_OUTPUT_NAME)
		endif (OS2_TARGET_SHORT)
		if (OS2_TARGET_SHORT)
			set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME "${OS2_TARGET_SHORT}")
		endif (OS2_TARGET_SHORT)
		if (OS2_TARGET_SHORT)
			set(_os2_plugin_def_name "${OS2_TARGET_SHORT}")
		else()
			set(_os2_plugin_def_name "${PLUGIN_NAME}")
		endif()
		set(_os2_plugin_def_source "${CMAKE_CURRENT_SOURCE_DIR}/${_os2_plugin_def_name}.def")
		if (NOT EXISTS "${_os2_plugin_def_source}" AND NOT _os2_plugin_def_name STREQUAL PLUGIN_NAME)
			set(_os2_plugin_def_source "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_NAME}.def")
		endif()
		if (EXISTS "${_os2_plugin_def_source}")
			file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
			configure_file("${_os2_plugin_def_source}"
				"${CMAKE_BINARY_DIR}/plugins/${_os2_plugin_def_name}.def" COPYONLY)
		endif()
	endif (OS2)
	set_target_properties(${PLUGIN_NAME} PROPERTIES
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
	target_link_libraries(${PLUGIN_NAME} PRIVATE
		${VACUUM_UTILS_NAME}
		Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Sql
		${ADD_LIBS})
	if (OS2)
		find_program(OS2_EMXIMP_EXECUTABLE emximp)
		if (NOT OS2_EMXIMP_EXECUTABLE)
			message(FATAL_ERROR "OS/2 plugin target ${PLUGIN_NAME} requires emximp")
		endif()
		add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
			COMMAND "${OS2_EMXIMP_EXECUTABLE}" -p128 -o
				"$<TARGET_LINKER_FILE:${PLUGIN_NAME}>"
				"$<TARGET_FILE:${PLUGIN_NAME}>"
			COMMENT "Generating OMF import library for ${PLUGIN_NAME}"
			VERBATIM)
	endif (OS2)
	if (WIN32)
		install(TARGETS ${PLUGIN_NAME}
			RUNTIME DESTINATION "${INSTALL_PLUGINS}"
			COMPONENT ${PLUGIN_NAME})
	endif (WIN32)
	if (OS2)
		install(TARGETS ${PLUGIN_NAME}
			RUNTIME DESTINATION "${INSTALL_PLUGINS}"
			COMPONENT ${PLUGIN_NAME})
	endif (OS2)
	if (UNIX AND NOT APPLE AND NOT OS2)
		install(TARGETS ${PLUGIN_NAME}
			LIBRARY DESTINATION "${INSTALL_PLUGINS}"
			COMPONENT ${PLUGIN_NAME})
	endif (UNIX AND NOT APPLE AND NOT OS2)
endif (IS_ENABLED)

if (${PLUGIN_NAME}_IS_ESSENTIAL)
	set(PLUGIN_GROUP essential_plugins)
else (${PLUGIN_NAME}_IS_ESSENTIAL)
	set(PLUGIN_GROUP optional_plugins)
endif (${PLUGIN_NAME}_IS_ESSENTIAL)

set(ALL_PLUGINS ${ALL_PLUGINS} ${PLUGIN_NAME} PARENT_SCOPE)
set(${PLUGIN_NAME}_DEPENDENCIES ${PLUGIN_DEPENDENCIES} PARENT_SCOPE)

cpack_add_component(${PLUGIN_NAME}
	DISPLAY_NAME "${PLUGIN_DISPLAY_NAME}"
	DESCRIPTION "${PLUGIN_DESCRIPTION}"
	GROUP ${PLUGIN_GROUP}
	DEPENDS ${VACUUM_UTILS_NAME} ${PLUGIN_DEPENDENCIES})
