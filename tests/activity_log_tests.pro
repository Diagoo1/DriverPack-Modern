QT = core
CONFIG += console c++11
CONFIG -= app_bundle
TARGET = DriverPackActivityLogTests
DEFINES += QT_NO_CAST_FROM_ASCII
SOURCES += activity_log_tests.cpp ../src/activity_log_data.cpp
QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++
