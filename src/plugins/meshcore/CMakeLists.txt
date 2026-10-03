project(meshcore)

set(PLUGIN_NAME "meshcore")
set(PLUGIN_DISPLAY_NAME "MeshCore")
set(PLUGIN_DESCRIPTION "MeshCore Bluetooth and USB chat support")
set(PLUGIN_DEPENDENCIES accountmanager)

find_package(Qt6 REQUIRED COMPONENTS SerialPort Widgets Xml)
list(APPEND ADD_LIBS Qt6::SerialPort)

if (NOT OS2)
	find_package(Qt6 REQUIRED COMPONENTS Bluetooth)
	list(APPEND ADD_LIBS Qt6::Bluetooth)
endif()

include("meshcore.cmake")

# MeshCore currently has no translation catalogs.
set(USED_LANGS "")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(meshcore_codec_tests EXCLUDE_FROM_ALL
		meshcorecodec_test.cpp
		meshcorecodec.cpp)
	target_compile_features(meshcore_codec_tests PRIVATE cxx_std_11)

	add_executable(meshcore_protocol_tests EXCLUDE_FROM_ALL
		meshcoreprotocol_test.cpp
		meshcoreplugin.cpp
		meshcoreplugin.h
		meshcorejoindialog.cpp
		meshcorejoindialog.h
		meshcore.h
		meshcore.cpp
		meshcorecodec.cpp
		meshcoretransport.h
		meshcoreserialtransport.h
		meshcoreserialtransport.cpp)
	target_compile_features(meshcore_protocol_tests PRIVATE cxx_std_11)
	target_link_libraries(meshcore_protocol_tests PRIVATE ${VACUUM_UTILS_NAME} Qt6::SerialPort Qt6::Widgets Qt6::Xml)
	add_executable(meshcore_join_dialog_tests EXCLUDE_FROM_ALL
		meshcorejoindialog_test.cpp
		meshcorejoindialog.cpp
		meshcorejoindialog.h)
	target_compile_features(meshcore_join_dialog_tests PRIVATE cxx_std_11)
	target_link_libraries(meshcore_join_dialog_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
	if (NOT OS2)
		target_sources(meshcore_protocol_tests PRIVATE
			meshcorebletransport.h
			meshcorebletransport.cpp)
		target_link_libraries(meshcore_protocol_tests PRIVATE Qt6::Bluetooth)
	endif()
endif()
