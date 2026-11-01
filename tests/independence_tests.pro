QT = core
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = DriverPackIndependenceTests
DEFINES += QT_NO_CAST_FROM_ASCII SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION
SOURCES += independence_tests.cpp ../src/drop_target.cpp ../src/portable_files.cpp ../native/driverpack_catalog.cpp ../native/sqlite/sqlite3.c
QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++
