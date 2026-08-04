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
    src/clipboard.cpp \
    src/directorymodel.cpp \
    src/fileops.cpp \
    src/formatting.cpp \
    src/fuzzyscorer.cpp \
    src/journal.cpp \
    src/lister.cpp \
    src/location.cpp \
    src/opener.cpp \
    src/searchengine.cpp \
    src/searchmodel.cpp \
    src/operations.cpp \
    src/theme.cpp \
    src/trash.cpp \
    src/watcher.cpp

HEADERS += \
    src/clipboard.h \
    src/directorymodel.h \
    src/entry.h \
    src/fileops.h \
    src/formatting.h \
    src/fuzzyscorer.h \
    src/journal.h \
    src/lister.h \
    src/location.h \
    src/opener.h \
    src/searchengine.h \
    src/searchmodel.h \
    src/operations.h \
    src/theme.h \
    src/trash.h \
    src/watcher.h

RESOURCES += src/resources.qrc
