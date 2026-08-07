# quick only. quickcontrols2 was here from the skeleton and nothing ever imported
# QtQuick.Controls — ContextMenu.qml is written in plain QtQuick specifically to avoid it
# (§12). Linking it anyway made libQt6QuickControls2 and libQt6QuickTemplates2 load-time
# dependencies of every start, which is the one budget with no room in it.
QT += quick dbus
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
    src/bulkrename.cpp \
    src/clipboard.cpp \
    src/directorymodel.cpp \
    src/filemanager1.cpp \
    src/fileops.cpp \
    src/formatting.cpp \
    src/handlers.cpp \
    src/hosts.cpp \
    src/fuzzyscorer.cpp \
    src/journal.cpp \
    src/lister.cpp \
    src/location.cpp \
    src/mounts.cpp \
    src/opener.cpp \
    src/places.cpp \
    src/preview.cpp \
    src/searchengine.cpp \
    src/searchmodel.cpp \
    src/operations.cpp \
    src/settings.cpp \
    src/terminal.cpp \
    src/theme.cpp \
    src/udisks.cpp \
    src/thumbnails.cpp \
    src/trash.cpp \
    src/watcher.cpp

HEADERS += \
    src/bulkrename.h \
    src/clipboard.h \
    src/directorymodel.h \
    src/entry.h \
    src/filemanager1.h \
    src/fileops.h \
    src/formatting.h \
    src/handlers.h \
    src/hosts.h \
    src/fuzzyscorer.h \
    src/journal.h \
    src/lister.h \
    src/location.h \
    src/mounts.h \
    src/opener.h \
    src/places.h \
    src/preview.h \
    src/previewimageprovider.h \
    src/searchengine.h \
    src/searchmodel.h \
    src/operations.h \
    src/settings.h \
    src/terminal.h \
    src/theme.h \
    src/udisks.h \
    src/thumbnails.h \
    src/trash.h \
    src/watcher.h

RESOURCES += src/resources.qrc
