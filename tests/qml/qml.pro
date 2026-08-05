# The QML suite is a second binary rather than another suite in tst_omafile, because it
# needs a QGuiApplication and a scene graph where that one deliberately runs headless under
# QCoreApplication. `bin/test` builds and runs both.
QT += qml quick qmltest
CONFIG += c++17 console
CONFIG -= app_bundle debug_and_release

TARGET = tst_omafile_qml
TEMPLATE = app

DESTDIR = $$PWD/../../build/tests
OBJECTS_DIR = $$PWD/../../build/tests/.qmlobj
MOC_DIR = $$PWD/../../build/tests/.qmlmoc

INCLUDEPATH += $$PWD/../../src

# Where the .qml test files live, so the binary can be run from anywhere.
DEFINES += QUICK_TEST_SOURCE_DIR=\\\"$$PWD\\\"

SOURCES += \
    main.cpp \
    ../../src/theme.cpp

HEADERS += \
    ../../src/theme.h
