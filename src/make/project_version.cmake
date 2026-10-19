if(NOT DEFINED PROJECT_ROOT)
	get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

set(_project_cmake "${PROJECT_ROOT}/CMakeLists.txt")
if(NOT EXISTS "${_project_cmake}")
	message(FATAL_ERROR "Cannot find project CMakeLists.txt: ${_project_cmake}")
endif()

file(STRINGS "${_project_cmake}" _version_declarations
	REGEX "^[ \t]*set\\(VERSION[ \t]+\"?[^\" )]+")
if(NOT _version_declarations)
	message(FATAL_ERROR "No set(VERSION ...) declaration found in ${_project_cmake}")
endif()

list(GET _version_declarations 0 _version_declaration)
string(REGEX REPLACE "^[ \t]*set\\(VERSION[ \t]+\"?([^\" )]+)\"?\\).*$" "\\1"
	_project_version "${_version_declaration}")
if(NOT _project_version MATCHES "^[0-9]+(\\.[0-9]+)*$")
	message(FATAL_ERROR "Invalid project VERSION '${_project_version}' in ${_project_cmake}")
endif()

message("${_project_version}")
