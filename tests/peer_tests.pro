TEMPLATE=app
TARGET=DriverPackPeerTests
DESTDIR=release
QT=core
CONFIG+=console release c++11
CONFIG-=debug debug_and_release app_bundle
DEFINES+=QT_NO_CAST_FROM_ASCII SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION
SOURCES+=peer_tests.cpp ../src/online_packages.cpp ../src/online_torrent.cpp ../native/driverpack_catalog.cpp ../native/sqlite/sqlite3.c
isEmpty(DRIVERPACK_TORRENT_PRI):error(Provide DRIVERPACK_TORRENT_PRI for the static peer transport.)
include($$DRIVERPACK_TORRENT_PRI)
win32:LIBS+=-lwininet
win32:QMAKE_LFLAGS+=-static -static-libgcc -static-libstdc++
