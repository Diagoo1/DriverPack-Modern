TEMPLATE=app
TARGET=DriverPackDeviceScopeTests
DESTDIR=release
QT=core
CONFIG+=console release c++11
CONFIG-=debug debug_and_release app_bundle
SOURCES+=device_installer_tests.cpp
QMAKE_LFLAGS+=-static -static-libgcc -static-libstdc++
