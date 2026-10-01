include(../../make/config.inc)

TARGET         = idle
TEMPLATE       = lib
CONFIG        += staticlib warn_off
DESTDIR        = ../../libs
unix:!macx:!haiku:!os2 {
  DEFINES     += HAVE_XSS
}
include(idle.pri)
