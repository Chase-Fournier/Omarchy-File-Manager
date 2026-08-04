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
    tst_formatting.cpp \
    tst_location.cpp \
    tst_theme.cpp \
    ../src/directorymodel.cpp \
    ../src/formatting.cpp \
    ../src/lister.cpp \
    ../src/location.cpp \
    ../src/opener.cpp \
    ../src/theme.cpp \
    ../src/watcher.cpp

HEADERS += \
    tst_directorymodel.h \
    tst_formatting.h \
    tst_location.h \
    tst_theme.h \
    ../src/directorymodel.h \
    ../src/entry.h \
    ../src/formatting.h \
    ../src/lister.h \
    ../src/location.h \
    ../src/opener.h \
    ../src/theme.h \
    ../src/watcher.h
