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
    tst_bulkrename.cpp \
    tst_directorymodel.cpp \
    tst_fileops.cpp \
    tst_formatting.cpp \
    tst_fuzzyscorer.cpp \
    tst_location.cpp \
    tst_polish.cpp \
    tst_remote.cpp \
    tst_search.cpp \
    tst_theme.cpp \
    ../src/bulkrename.cpp \
    ../src/directorymodel.cpp \
    ../src/fileops.cpp \
    ../src/formatting.cpp \
    ../src/handlers.cpp \
    ../src/thumbnails.cpp \
    ../src/hosts.cpp \
    ../src/mounts.cpp \
    ../src/fuzzyscorer.cpp \
    ../src/journal.cpp \
    ../src/lister.cpp \
    ../src/location.cpp \
    ../src/clipboard.cpp \
    ../src/opener.cpp \
    ../src/searchengine.cpp \
    ../src/operations.cpp \
    ../src/theme.cpp \
    ../src/trash.cpp \
    ../src/watcher.cpp

HEADERS += \
    tst_bulkrename.h \
    tst_directorymodel.h \
    tst_fileops.h \
    tst_formatting.h \
    tst_fuzzyscorer.h \
    tst_location.h \
    tst_polish.h \
    tst_remote.h \
    tst_search.h \
    tst_theme.h \
    ../src/bulkrename.h \
    ../src/directorymodel.h \
    ../src/entry.h \
    ../src/fileops.h \
    ../src/formatting.h \
    ../src/handlers.h \
    ../src/thumbnails.h \
    ../src/hosts.h \
    ../src/mounts.h \
    ../src/fuzzyscorer.h \
    ../src/journal.h \
    ../src/lister.h \
    ../src/location.h \
    ../src/clipboard.h \
    ../src/opener.h \
    ../src/searchengine.h \
    ../src/operations.h \
    ../src/theme.h \
    ../src/trash.h \
    ../src/watcher.h
