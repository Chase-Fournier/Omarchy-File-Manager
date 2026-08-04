QT += quick quickcontrols2
CONFIG += c++17 qtquickcompiler
CONFIG -= debug_and_release

TARGET = omafile
TEMPLATE = app

# Keep the build tree out of the source tree; bin/build points here.
DESTDIR = $$PWD/build
OBJECTS_DIR = $$PWD/build/.obj
MOC_DIR = $$PWD/build/.moc
RCC_DIR = $$PWD/build/.rcc

QMAKE_CXXFLAGS_RELEASE += -O2

SOURCES += \
    src/main.cpp \
    src/directorymodel.cpp \
    src/formatting.cpp \
    src/lister.cpp \
    src/location.cpp \
    src/opener.cpp \
    src/theme.cpp \
    src/watcher.cpp

HEADERS += \
    src/directorymodel.h \
    src/entry.h \
    src/formatting.h \
    src/lister.h \
    src/location.h \
    src/opener.h \
    src/theme.h \
    src/watcher.h

RESOURCES += src/resources.qrc
