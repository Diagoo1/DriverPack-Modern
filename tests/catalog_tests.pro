QT = core
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = DriverPackCatalogTests
DEFINES += QT_NO_CAST_FROM_ASCII SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION
SOURCES += catalog_tests.cpp ../native/driverpack_catalog.cpp ../native/plan_validation.cpp ../native/sqlite/sqlite3.c
QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++
