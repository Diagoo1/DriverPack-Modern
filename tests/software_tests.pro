QT = core
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = DriverPackSoftwareTests
DEFINES += QT_NO_CAST_FROM_ASCII
SOURCES += software_tests.cpp ../src/software_catalog.cpp
LIBS += -ladvapi32
QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++

RESOURCES += software_fixture.qrc
