QT = core
CONFIG += console c++11
CONFIG -= app_bundle
TARGET = DriverPackSessionTests
DEFINES += QT_NO_CAST_FROM_ASCII
SOURCES += diagnostic_sessions_tests.cpp ../src/diagnostic_sessions.cpp
QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++
