file(GLOB HEADERS "*.h")
list(REMOVE_ITEM HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/version.h")

if (INSTALL_SDK)
	install(FILES ${HEADERS}
		"${CMAKE_BINARY_DIR}/src/definitions/version.h"
		DESTINATION "${INSTALL_INCLUDES}/definitions"
		COMPONENT sdk)
endif (INSTALL_SDK)
