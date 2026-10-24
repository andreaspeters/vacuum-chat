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

# OS/2-specific additions for libolm runtime DLLs
if (OS2 AND OLM_FOUND)
	set(VACUUM_OS2_RUNTIME_DLL_DIR "" CACHE PATH
		"Directory containing external OS/2 runtime DLLs such as libolm.dll")
	if (NOT VACUUM_OS2_RUNTIME_DLL_DIR)
		message(FATAL_ERROR "Set VACUUM_OS2_RUNTIME_DLL_DIR when libolm is enabled for OS/2")
	endif()
	set(_vacuum_libolm_dll "${VACUUM_OS2_RUNTIME_DLL_DIR}/libolm.dll")
	if (NOT EXISTS "${_vacuum_libolm_dll}")
		message(FATAL_ERROR "Missing required OS/2 libolm runtime DLL: ${_vacuum_libolm_dll}")
	endif()
	install(FILES "${_vacuum_libolm_dll}"
		DESTINATION "${INSTALL_BINS}")
	if (EXISTS "${VACUUM_OS2_RUNTIME_DLL_DIR}/olm.dll")
		install(FILES "${VACUUM_OS2_RUNTIME_DLL_DIR}/olm.dll"
			DESTINATION "${INSTALL_BINS}")
	endif()
endif()

if(OpenSSL_FOUND)
	message(STATUS "Found OpenSSL for Matrix SSSS")
	add_definitions(-DHAVE_OPENSSL=1)
	set(ADD_LIBS ${ADD_LIBS} OpenSSL::Crypto)
else()
	message(STATUS "OpenSSL not found - Matrix SSSS support disabled")
	add_definitions(-DHAVE_OPENSSL=0)
endif()

if (OS2 AND OpenSSL_FOUND)
	get_filename_component(_vacuum_openssl_crypto_dir "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
	set(_vacuum_openssl_crypto_dll "${_vacuum_openssl_crypto_dir}/crypto3.dll")
	if (NOT EXISTS "${_vacuum_openssl_crypto_dll}")
		message(FATAL_ERROR "Missing OS/2 OpenSSL runtime DLL: ${_vacuum_openssl_crypto_dll}")
	endif()
	install(FILES "${_vacuum_openssl_crypto_dll}"
		DESTINATION "${INSTALL_BINS}")
	if (OPENSSL_SSL_LIBRARY)
		get_filename_component(_vacuum_openssl_ssl_dir "${OPENSSL_SSL_LIBRARY}" DIRECTORY)
		set(_vacuum_openssl_ssl_dll "${_vacuum_openssl_ssl_dir}/ssl3.dll")
		if (NOT EXISTS "${_vacuum_openssl_ssl_dll}")
			message(FATAL_ERROR "Missing OS/2 OpenSSL runtime DLL: ${_vacuum_openssl_ssl_dll}")
		endif()
		install(FILES "${_vacuum_openssl_ssl_dll}"
			DESTINATION "${INSTALL_BINS}")
	endif()
	set(_vacuum_openssl_pthread_dll "${_vacuum_openssl_crypto_dir}/pthr01.dll")
	if (EXISTS "${_vacuum_openssl_pthread_dll}")
		install(FILES "${_vacuum_openssl_pthread_dll}"
			DESTINATION "${INSTALL_BINS}")
	endif()
endif()

include("matrix.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")

if (BUILD_TESTING)
	set(MATRIX_TEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests/matrix")

	function(add_matrix_plugin_test_target target_name source_file)
		add_executable(${target_name} EXCLUDE_FROM_ALL ${source_file})
		target_include_directories(${target_name} PRIVATE
			"${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
		target_compile_features(${target_name} PRIVATE cxx_std_17)
		target_link_libraries(${target_name} PRIVATE
			matrix ${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Sql ${ADD_LIBS})
	endfunction()

	add_matrix_plugin_test_target(matrix_history_export_tests "${MATRIX_TEST_SOURCE_DIR}/matrix_history_export_test.cpp")
	add_matrix_plugin_test_target(matrix_network_history_backup_tests "${MATRIX_TEST_SOURCE_DIR}/matrix_network_history_backup_test.cpp")
	target_sources(matrix_network_history_backup_tests PRIVATE
		../messagearchiver/protocolhistoryviewmodel.cpp)
	target_include_directories(matrix_network_history_backup_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src/plugins/messagearchiver")

	add_executable(matrix_room_api_tests EXCLUDE_FROM_ALL
		"${MATRIX_TEST_SOURCE_DIR}/matrix_room_api_tests.cpp" matrixroomapi.cpp
		"${MATRIX_TEST_SOURCE_DIR}/matrix_policy_test.cpp" matrixmetadatarefreshpolicy.cpp)
	target_include_directories(matrix_room_api_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(matrix_room_api_tests PRIVATE cxx_std_17)
	target_link_libraries(matrix_room_api_tests PRIVATE Qt6::Core Qt6::Network Qt6::Xml)
	add_test(NAME matrix_room_api_tests COMMAND matrix_room_api_tests)

	add_executable(matrix_join_dialog_tests EXCLUDE_FROM_ALL
		"${MATRIX_TEST_SOURCE_DIR}/matrixjoinroomchatdialog_test.cpp" matrixjoinroomchatdialog.cpp matrixroomapi.cpp)
	target_include_directories(matrix_join_dialog_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(matrix_join_dialog_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
	add_executable(matrix_context_tests EXCLUDE_FROM_ALL "${MATRIX_TEST_SOURCE_DIR}/matrixcontext_test.cpp" matrixcontext.cpp)
	target_include_directories(matrix_context_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(matrix_context_tests PRIVATE ${VACUUM_UTILS_NAME} Qt6::Core Qt6::Xml)
	add_executable(matrix_text_message_tests EXCLUDE_FROM_ALL
		"${MATRIX_TEST_SOURCE_DIR}/matrixtextmessage_test.cpp" matrixtextmessage.cpp ../../utils/matrixhtml.cpp)
	target_include_directories(matrix_text_message_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/plugins/matrix")
	target_compile_features(matrix_text_message_tests PRIVATE cxx_std_17)
	target_link_libraries(matrix_text_message_tests PRIVATE Qt6::Core Qt6::Gui)
	add_executable(matrix_edit_message_tests EXCLUDE_FROM_ALL
		"${MATRIX_TEST_SOURCE_DIR}/matrixeditmessage_test.cpp" matrixtextmessage.cpp ../../utils/matrixhtml.cpp)
	target_include_directories(matrix_edit_message_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(matrix_edit_message_tests PRIVATE cxx_std_17)
	target_link_libraries(matrix_edit_message_tests PRIVATE Qt6::Core Qt6::Gui)
	add_test(NAME matrix_edit_message_tests COMMAND matrix_edit_message_tests)
	add_executable(matrix_timestamp_tests EXCLUDE_FROM_ALL "${MATRIX_TEST_SOURCE_DIR}/matrix_timestamp_test.cpp")
	target_include_directories(matrix_timestamp_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(matrix_timestamp_tests PRIVATE cxx_std_11)
	target_link_libraries(matrix_timestamp_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql)

	# Sources shared by Matrix network integration tests.
	set(MATRIX_TEST_SOURCES
		matrixhistorybackup.cpp
		matrixnetwork.cpp
		matrixnetworkinvite.cpp
		matrixroomapi.cpp
		matrixdatabase.cpp
		matrixdatabaseworker.cpp
		matrixolm.cpp
		matrixssss.cpp
	)

	# Helper function to add Matrix test targets with shared configuration.
	function(add_matrix_test_target target_name source_file)
		add_executable(${target_name} EXCLUDE_FROM_ALL
			${source_file} ${MATRIX_TEST_SOURCES})
		target_include_directories(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
		target_link_libraries(${target_name} PRIVATE
			${VACUUM_UTILS_NAME} Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql ${ADD_LIBS})
	endfunction()

	add_matrix_test_target(matrix_history_retry_tests "${MATRIX_TEST_SOURCE_DIR}/matrixhistoryretry_test.cpp")
	add_matrix_test_target(matrix_room_encryption_tests "${MATRIX_TEST_SOURCE_DIR}/matrix_room_encryption_test.cpp")
	add_matrix_test_target(matrix_network_login_tests "${MATRIX_TEST_SOURCE_DIR}/matrix_network_login_tests.cpp")
	if (OLM_FOUND)
		add_matrix_test_target(matrix_avatar_cache_tests "${MATRIX_TEST_SOURCE_DIR}/matrixavatarcache_test.cpp")
	endif()
	add_matrix_test_target(matrix_account_avatar_tests "${MATRIX_TEST_SOURCE_DIR}/matrixaccountavatar_test.cpp")

	add_matrix_plugin_test_target(matrix_presence_tests "${MATRIX_TEST_SOURCE_DIR}/matrix_presence_lifecycle_test.cpp")

	add_executable(matrix_history_tests EXCLUDE_FROM_ALL
		"${MATRIX_TEST_SOURCE_DIR}/matrixhistorypagination_test.cpp"
		matrixhistorybackup.cpp
		matrixdatabase.cpp
		matrixdatabaseworker.cpp
	)
	target_include_directories(matrix_history_tests PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(matrix_history_tests PRIVATE cxx_std_17)
	target_link_libraries(matrix_history_tests PRIVATE Qt6::Core Qt6::Network Qt6::Gui Qt6::Sql)

	add_matrix_plugin_test_target(matrix_sync_batch_tests "${MATRIX_TEST_SOURCE_DIR}/matrix_sync_batch_test.cpp")
	add_executable(matrix_member_display_name_cache_tests EXCLUDE_FROM_ALL
		"${MATRIX_TEST_SOURCE_DIR}/matrix_member_display_name_cache_test.cpp")
	target_include_directories(matrix_member_display_name_cache_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(matrix_member_display_name_cache_tests PRIVATE cxx_std_17)
	target_link_libraries(matrix_member_display_name_cache_tests PRIVATE Qt6::Core)
	add_test(NAME matrix_member_display_name_cache_tests COMMAND matrix_member_display_name_cache_tests)
endif()

if(TARGET matrix)
	if(OLM_INCLUDE_DIR)
		target_include_directories(matrix PRIVATE "${OLM_INCLUDE_DIR}")
	elseif(OLM_ROOT)
		target_include_directories(matrix PRIVATE "${OLM_ROOT}/include")
	endif()
endif()
