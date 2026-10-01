include("${CMAKE_SOURCE_DIR}/src/make/config.cmake")

project(jdns C CXX)

add_definitions("-DQDNS_NO_COMPAT")
include_directories(${CMAKE_CURRENT_BINARY_DIR})

include("jdns.cmake")

# Qt6 for MOC
find_package(Qt6 COMPONENTS Network REQUIRED)
add_library(jdns STATIC ${SOURCES} ${HEADERS})

target_link_libraries(jdns PRIVATE Qt6::Core Qt6::Network ${LIBS})
set_target_properties(jdns PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(jdns PROPERTIES
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/libs"
	COMPILE_FLAGS "-w ${CMAKE_SHARED_LIBRARY_C_FLAGS}"
	LINK_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
