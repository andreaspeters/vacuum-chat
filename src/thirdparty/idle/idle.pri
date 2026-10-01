HEADERS += idle.h
SOURCES += idle.cpp

unix:!macx:!os2 {
	SOURCES += idle_x11.cpp
}
win32 {
	SOURCES += idle_win.cpp
}
mac {
	SOURCES += idle_mac.cpp
}
os2 {
	SOURCES += idle_os2.cpp
}
