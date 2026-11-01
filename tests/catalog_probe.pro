QT = core
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = DriverPackCatalogProbe
DEFINES += QT_NO_CAST_FROM_ASCII SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION Z7_ST
SOURCES += catalog_probe.cpp ../native/driverpack_catalog.cpp ../native/driverpack_devices.cpp ../native/driverpack_archive.cpp ../native/driverpack_core.cpp ../native/plan_validation.cpp ../native/sqlite/sqlite3.c
include(../native/sevenzip.pri)
RESOURCES += ../driverpack_resources.qrc
LIBS += -lsetupapi -lcfgmgr32 -ladvapi32 -lshell32 -lnewdev -lole32
QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++
