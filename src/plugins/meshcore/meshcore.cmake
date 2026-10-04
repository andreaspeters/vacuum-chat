set(SOURCES meshcoreplugin.cpp meshcore.cpp meshcorecodec.cpp
	meshcorejoindialog.cpp meshcoreserialtransport.cpp)
set(HEADERS meshcoreplugin.h meshcore.h meshcorecodec.h meshcorejoindialog.h
	meshcoretransport.h meshcoreserialtransport.h)
if (NOT OS2)
	set(SOURCES ${SOURCES}
		${CMAKE_CURRENT_SOURCE_DIR}/meshcorebletransport.cpp)
	set(HEADERS ${HEADERS}
		${CMAKE_CURRENT_SOURCE_DIR}/meshcorebletransport.h)
	add_definitions(-DMESHCORE_WITH_BLE)
endif()
