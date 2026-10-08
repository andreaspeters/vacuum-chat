include(config.inc)

#Project Configuration
TARGET              = otre2e
TEMPLATE            = lib
CONFIG             += plugin
QT                  = core xml gui

isEmpty(VACUUM_BUILD_PATH) {
    LIBS               += -L$$INSTALL_PREFIX/$$INSTALL_APP_DIR/$$INSTALL_LIB_DIR
    LIBS               += -l$${TARGET_UTILS} -lotr  -lgcrypt -lgpg-error -lvacuumu.dll
} else {
# WINDOWS !
    LIBS               += -L$${VACUUM_BUILD_PATH}
    LIBS               += -l$${TARGET_UTILS} -lotr -lgcrypt -lgpg-error -lvacuumu.dll 
}

isEmpty(VACUUM_SOURCE_PATH) {
    DEPENDPATH         += $$INSTALL_PREFIX/include/$$INSTALL_APP_DIR
    INCLUDEPATH        += $$INSTALL_PREFIX/include/$$INSTALL_APP_DIR
} else {
# WINDOWS !
    DEPENDPATH         += $${VACUUM_SOURCE_PATH}/sdk
    INCLUDEPATH        += $${VACUUM_SOURCE_PATH}/sdk
}

#Install

win32 {
  INSTALL_PLUGINS          = $$INSTALL_PREFIX/$$INSTALL_APP_DIR/$$INSTALL_LIB_DIR/plugins
#  INSTALL_RESOURCES        = $$INSTALL_PREFIX/$$INSTALL_APP_DIR/$$INSTALL_RES_DIR/resources
#  INSTALL_DOCUMENTS        = $$INSTALL_PREFIX/$$INSTALL_APP_DIR
  INSTALL_TRANSLATIONS     = $$INSTALL_PREFIX/$$INSTALL_APP_DIR/$$INSTALL_RES_DIR/translations
} else:unix:!macx {
  INSTALL_PLUGINS          = $$INSTALL_PREFIX/$$INSTALL_LIB_DIR/$$INSTALL_APP_DIR/plugins
#  INSTALL_RESOURCES        = $$INSTALL_PREFIX/$$INSTALL_RES_DIR/$$INSTALL_APP_DIR/resources
#  INSTALL_DOCUMENTS        = $$INSTALL_PREFIX/$$INSTALL_RES_DIR/doc/$$INSTALL_APP_DIR
  INSTALL_TRANSLATIONS     = $$INSTALL_PREFIX/$$INSTALL_RES_DIR/$$INSTALL_APP_DIR/translations
} else:macx {
  INSTALL_PLUGINS          = $$INSTALL_PREFIX/$$INSTALL_APP_DIR/Contents/PlugIns
#  INSTALL_RESOURCES        = $$INSTALL_PREFIX/$$INSTALL_APP_DIR/Contents/$$INSTALL_RES_DIR
#  INSTALL_DOCUMENTS        = $$INSTALL_PREFIX/$$INSTALL_APP_DIR/Contents/$$INSTALL_RES_DIR
  INSTALL_TRANSLATIONS     = $$INSTALL_PREFIX/$$INSTALL_APP_DIR/Contents/$$INSTALL_RES_DIR/translations
} else:os2 {
  INSTALL_PLUGINS          = $$INSTALL_PREFIX/$$INSTALL_APP_DIR/$$INSTALL_LIB_DIR/plugins
  INSTALL_TRANSLATIONS     = $$INSTALL_PREFIX/$$INSTALL_APP_DIR/$$INSTALL_RES_DIR/translations

}

target.path        = $$INSTALL_PLUGINS
#resources.path     = $$INSTALL_RESOURCES
#resources.files    = ./resources/*
#documents.path     = $$INSTALL_DOCUMENTS
#documents.files    = ./doc/*
#translations.path  = $$INSTALL_TRANSLATIONS
#translations.files = ./translations/*

#INSTALLS            = target translations
INSTALLS             = target

#Translation
TRANS_SOURCE_ROOT = .
include(./translations.inc)

include(otre2e.pri)
