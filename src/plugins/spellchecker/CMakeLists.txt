project(spellchecker)

set(PLUGIN_NAME "spellchecker")
set(PLUGIN_DISPLAY_NAME "Spellchecker")
set(PLUGIN_DEPENDENCIES messagewidgets) # used only in CPack 

if (UNIX AND NOT APPLE)
	set(SPELLCHECKER_BACKEND "ENCHANT" CACHE STRING "Name of engine that will be used in Spellchecker plugin")
else (UNIX AND NOT APPLE)
	set(SPELLCHECKER_BACKEND "HUNSPELL" CACHE STRING "Name of engine that will be used in Spellchecker plugin")
endif (UNIX AND NOT APPLE)

string(TOUPPER ${SPELLCHECKER_BACKEND} SPELLCHECKER_BACKEND)

include("spellchecker.cmake")

if (SPELLCHECKER_BACKEND STREQUAL "ENCHANT" AND UNIX)
	FIND_PACKAGE(PkgConfig)
	pkg_check_modules(SYSTEM_ENCHANT enchant>=1.5.0)
	if (NOT SYSTEM_ENCHANT_FOUND)
		set(SPELLCHECKER_BACKEND "HUNSPELL")
		message(STATUS "System enchant not found, switching to hunspell")
	endif (NOT SYSTEM_ENCHANT_FOUND)
endif (SPELLCHECKER_BACKEND STREQUAL "ENCHANT" AND UNIX)

if (SPELLCHECKER_BACKEND STREQUAL "ASPELL")
	include("aspellchecker.cmake")
elseif (SPELLCHECKER_BACKEND STREQUAL "ENCHANT")
	include("enchantchecker.cmake")
elseif (SPELLCHECKER_BACKEND STREQUAL "MACSPELL")
	include("macspellchecker.cmake")
elseif (SPELLCHECKER_BACKEND STREQUAL "HUNSPELL")
	include("hunspellchecker.cmake")
else (SPELLCHECKER_BACKEND STREQUAL "ASPELL")
	message(FATAL_ERROR "Please specify correct backend name, only aspell, enchant, hunspell, macspell supported")
endif (SPELLCHECKER_BACKEND STREQUAL "ASPELL")

if (BUILD_TESTING AND SPELLCHECKER_BACKEND STREQUAL "HUNSPELL")
	set(SPELLCHECKER_TEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests/spellchecker")
	add_executable(hunspellchecker_highlighting_tests EXCLUDE_FROM_ALL
		${SPELLCHECKER_TEST_SOURCE_DIR}/hunspellchecker_highlighting_test.cpp
		hunspellchecker.cpp
		spellbackend.cpp
		spellhighlighter.cpp)
	target_include_directories(hunspellchecker_highlighting_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_link_libraries(hunspellchecker_highlighting_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml ${ADD_LIBS})
endif ()

include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")
