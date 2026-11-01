QT = core
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = DriverPackTransferTests
DEFINES += QT_NO_CAST_FROM_ASCII SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION
SOURCES += transfer_tests.cpp ../src/online_packages.cpp ../native/driverpack_catalog.cpp ../native/sqlite/sqlite3.c
LIBS += -lwininet
QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++
