file(GLOB SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "/moc_[^/]*\\.cpp$")
file(GLOB UIS "*.ui")
set(HEADERS "aboutbox.h"
		"pluginmanager.h"
		"setuppluginsdialog.h")

# Qt6's CMake integration handles MOC/UIC/RCC through the target properties
# enabled in the top-level project.
