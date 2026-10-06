project(matrix)

set(PLUGIN_NAME "matrix")
set(PLUGIN_DISPLAY_NAME "Matrix Client-Server")
set(PLUGIN_DEPENDENCIES) # optional for CPack

find_package(PkgConfig QUIET)
find_package(OpenSSL QUIET)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(OLM QUIET olm)
endif()
if(NOT OLM_FOUND)
	find_path(OLM_INCLUDE_DIR olm/olm.h HINTS ${OLM_ROOT} PATH_SUFFIXES include)
	find_library(OLM_LIBRARY NAMES olm HINTS ${OLM_ROOT} PATH_SUFFIXES lib)
	if(OLM_INCLUDE_DIR AND OLM_LIBRARY)
		set(OLM_FOUND TRUE)
	endif()
endif()

if(OLM_FOUND)
	message(STATUS "Found libolm: ${OLM_LIBRARY} ${OLM_LIBRARIES}")
	add_definitions(-DHAVE_OLM=1)
	if(OLM_INCLUDE_DIR)
		include_directories(${OLM_INCLUDE_DIR})
	endif()
	if(OLM_INCLUDE_DIRS)
		include_directories(${OLM_INCLUDE_DIRS})
	endif()
	if(OLM_LIBRARIES)
		set(ADD_LIBS ${ADD_LIBS} ${OLM_LIBRARIES})
	else()
		set(ADD_LIBS ${ADD_LIBS} ${OLM_LIBRARY})
	endif()
else()
	message(STATUS "libolm not found - Matrix E2EE support disabled")
	add_definitions(-DHAVE_OLM=0)
endif()

if(OpenSSL_FOUND)
	message(STATUS "Found OpenSSL for Matrix SSSS")
	add_definitions(-DHAVE_OPENSSL=1)
	set(ADD_LIBS ${ADD_LIBS} OpenSSL::Crypto)
else()
	message(STATUS "OpenSSL not found - Matrix SSSS support disabled")
	add_definitions(-DHAVE_OPENSSL=0)
endif()

include("matrix.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	add_executable(matrix_public_rooms_tests EXCLUDE_FROM_ALL
		matrixpublicrooms_test.cpp matrixpublicrooms.cpp)
	target_link_libraries(matrix_public_rooms_tests PRIVATE Qt6::Core Qt6::Network)
	add_executable(matrix_direct_room_tests EXCLUDE_FROM_ALL
		matrixdirectroom_test.cpp matrixdirectroom.cpp)
	target_link_libraries(matrix_direct_room_tests PRIVATE Qt6::Core Qt6::Network)
	add_executable(matrix_room_invite_tests EXCLUDE_FROM_ALL
		matrixroominvite_test.cpp matrixroominvite.cpp matrixdirectroom.cpp)
	target_link_libraries(matrix_room_invite_tests PRIVATE Qt6::Core Qt6::Network)
	add_executable(matrix_join_dialog_tests EXCLUDE_FROM_ALL
		matrixjoinroomchatdialog_test.cpp matrixjoinroomchatdialog.cpp matrixdirectroom.cpp
		matrixpublicrooms.cpp)
	target_link_libraries(matrix_join_dialog_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
	add_executable(matrix_context_tests EXCLUDE_FROM_ALL matrixcontext_test.cpp matrixcontext.cpp)
	target_link_libraries(matrix_context_tests PRIVATE ${VACUUM_UTILS_NAME} Qt6::Core Qt6::Xml)
	add_executable(matrix_text_message_tests EXCLUDE_FROM_ALL
		matrixtextmessage_test.cpp matrixtextmessage.cpp ../../utils/matrixhtml.cpp)
	target_include_directories(matrix_text_message_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/plugins/matrix")
	target_compile_features(matrix_text_message_tests PRIVATE cxx_std_17)
	target_link_libraries(matrix_text_message_tests PRIVATE Qt6::Core Qt6::Gui)
	add_executable(matrix_timestamp_tests EXCLUDE_FROM_ALL matrix_timestamp_test.cpp)
	target_include_directories(matrix_timestamp_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_compile_features(matrix_timestamp_tests PRIVATE cxx_std_11)
	target_link_libraries(matrix_timestamp_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql)
	add_executable(matrix_history_retry_tests EXCLUDE_FROM_ALL
		matrixhistoryretry_test.cpp matrixnetwork.cpp matrixdatabase.cpp matrixdatabaseworker.cpp
		matrixolm.cpp matrixssss.cpp matrixdirectroom.cpp matrixpublicrooms.cpp)
	target_include_directories(matrix_history_retry_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
	target_link_libraries(matrix_history_retry_tests PRIVATE
		${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql ${ADD_LIBS})
	if (OLM_FOUND)
		add_executable(matrix_avatar_cache_tests EXCLUDE_FROM_ALL
			matrixavatarcache_test.cpp matrixnetwork.cpp matrixdatabase.cpp matrixdatabaseworker.cpp
			matrixolm.cpp matrixssss.cpp matrixdirectroom.cpp matrixpublicrooms.cpp)
		target_link_libraries(matrix_avatar_cache_tests PRIVATE
			${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql ${ADD_LIBS})
	endif()
endif()

if(TARGET matrix)
	if(OLM_INCLUDE_DIR)
		target_include_directories(matrix PRIVATE "${OLM_INCLUDE_DIR}")
	elseif(OLM_ROOT)
		target_include_directories(matrix PRIVATE "${OLM_ROOT}/include")
	endif()
endif()