file(GLOB SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "/moc_[^/]*\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "/messagenotificationmute_test\\.cpp$")

set(HEADERS "action.h"
            "filestorage.h"
            "iconstorage.h"
            "menu.h"
            "menubarchanger.h"
            "options.h"
            "shortcuts.h"
            "statusbarchanger.h"
            "systemmanager.h"
            "toolbarchanger.h"
            "animatedtextbrowser.h"
            "closebutton.h"
            "searchlineedit.h"
            "imagemanager.h"
            "advanceditemdelegate.h"
            "matrixhtml.h")

# Qt6's CMake integration handles MOC through AUTOMOC.
