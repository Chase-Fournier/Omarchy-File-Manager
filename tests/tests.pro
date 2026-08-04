QT += testlib gui
QT -= quick
CONFIG += c++17 console
CONFIG -= app_bundle debug_and_release

TARGET = tst_omafile
TEMPLATE = app

DESTDIR = $$PWD/../build/tests
OBJECTS_DIR = $$PWD/../build/tests/.obj
MOC_DIR = $$PWD/../build/tests/.moc

INCLUDEPATH += $$PWD/../src

SOURCES += \
    main.cpp \
    tst_directorymodel.cpp \
    tst_fileops.cpp \
    tst_formatting.cpp \
    tst_location.cpp \
    tst_theme.cpp \
    ../src/directorymodel.cpp \
    ../src/fileops.cpp \
    ../src/formatting.cpp \
    ../src/journal.cpp \
    ../src/lister.cpp \
    ../src/location.cpp \
    ../src/opener.cpp \
    ../src/theme.cpp \
    ../src/trash.cpp \
    ../src/watcher.cpp

HEADERS += \
    tst_directorymodel.h \
    tst_fileops.h \
    tst_formatting.h \
    tst_location.h \
    tst_theme.h \
    ../src/directorymodel.h \
    ../src/entry.h \
    ../src/fileops.h \
    ../src/formatting.h \
    ../src/journal.h \
    ../src/lister.h \
    ../src/location.h \
    ../src/opener.h \
    ../src/theme.h \
    ../src/trash.h \
    ../src/watcher.h
