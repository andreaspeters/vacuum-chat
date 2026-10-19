#ifndef DEF_VERSION_H
#define DEF_VERSION_H

#define CLIENT_NAME             "Vacuum Chat"
#ifndef CLIENT_VERSION
#error CLIENT_VERSION must be supplied by the build system from CMake VERSION
#endif
#define CLIENT_VERSION_SUFIX    ""
#define CLIENT_HOME_PAGE        "https://github.com/andreaspeters/vacuum-chat"

#endif
