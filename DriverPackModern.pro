# DriverPack Modern - Qt 5.6-compatible UI modified by Tarek Sadek
# Target: Qt 5.6.3 + C++11 + Windows XP x86 through Windows 11 (pending VM validation)
QT += core gui widgets svg
CONFIG += c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = DriverPackModern
DEFINES += QT_NO_CAST_FROM_ASCII

SOURCES += \
    src/online_torrent.cpp \
    src/online_packages.cpp \
    src/online_worker.cpp \
    src/support_report.cpp \
    src/appearance.cpp src/window_motion.cpp \
    src/single_instance.cpp \
    src/scan_report.cpp \
    src/automation_plan.cpp \
    src/portable_library.cpp src/portable_files.cpp \
    src/about_page.cpp \
    src/main.cpp \
    src/searchable_language_combo.cpp \
    src/system_profile.cpp \
    src/pnp_probe.cpp \
    src/origin_languages.cpp \
    src/device_scanner.cpp \
    src/engine_bridge.cpp \
    src/origin_worker.cpp \
    native/plan_validation.cpp \
    src/premium_popover.cpp \
    src/device_model.cpp \
    src/app_update.cpp \
    src/restart_prompt.cpp \
    src/drop_target.cpp \
    src/ui_scale.cpp \
    src/ui_font.cpp \
    src/ui_language.cpp \
    src/theme_pack.cpp \
    src/theme.cpp \
    src/ui_elements.cpp \
    src/vector_icons.cpp \
    src/driver_catalog.cpp \

HEADERS += \
    src/online_packages.h \
    src/online_worker.h \
    src/support_report.h \
    src/appearance.h src/window_motion.h \
    src/single_instance.h \
    src/scan_report.h \
    src/searchable_language_combo.h \
    src/system_profile.h \
    src/pnp_probe.h \
    src/origin_languages.h \
    src/device_scanner.h \
    src/engine_bridge.h \
    src/origin_worker.h \
    native/origin_api.h \
    src/premium_popover.h \
    src/device_model.h \
    src/theme.h \
    src/ui_elements.h \
    src/device_types.h \
    src/vector_icons.h \
    src/driver_catalog.h \


RESOURCES += icons.qrc

win32:LIBS += -lsetupapi -lcfgmgr32 -ladvapi32 -lole32 -lwininet
win32:RC_FILE = resources.rc
win32:QMAKE_LFLAGS += -Wl,--major-subsystem-version,5,--minor-subsystem-version,1
win32:QMAKE_LFLAGS += -Wl,--major-os-version,5,--minor-os-version,1

# Qt 5.6.3's static build must use the XP deployment target when building for XP.
# Linking against dynamic Qt will NOT produce the promised single-file portable executable.

DEFINES += DRIVERPACK_CATALOG_LINKED DRIVERPACK_INSTALL_AVAILABLE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION
SOURCES += native/driverpack_catalog.cpp native/driverpack_devices.cpp native/driverpack_archive.cpp native/driverpack_core.cpp native/sqlite/sqlite3.c
include(native/sevenzip.pri)
RESOURCES += driverpack_resources.qrc
win32:LIBS += -lnewdev -lshell32 -luuid -loleaut32
contains(CONFIG, single_file) {
    !contains(QT_CONFIG, static):error(A static Qt SDK is required for a single-file build.)
    QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++ -Wl,--gc-sections
    QMAKE_CFLAGS_RELEASE += -ffunction-sections -fdata-sections
    QMAKE_CXXFLAGS_RELEASE += -ffunction-sections -fdata-sections
    QTPLUGIN += qwindows qsvgicon
    DEFINES += SDIO_STATIC_QT
}

!isEmpty(DRIVERPACK_TORRENT_PRI):include($$quote($$DRIVERPACK_TORRENT_PRI))

SOURCES += src/software_catalog.cpp
HEADERS += src/software_catalog.h

SOURCES += src/hardware_report.cpp
HEADERS += src/hardware_report.h
win32:LIBS += -lwbemuuid

SOURCES += src/driverpack_shell.cpp
HEADERS += src/driverpack_shell.h

SOURCES += src/title_bar.cpp src/tools_drawer.cpp
HEADERS += src/title_bar.h src/tools_drawer.h

# qmake does not infer ICON dependencies from a custom RC_FILE.
win32 {
    CONFIG(debug, debug|release):icon_asset.target = debug/resources_res.o
    else:icon_asset.target = release/resources_res.o
    icon_asset.depends = $$PWD/assets/driverpack-modern.ico
    QMAKE_EXTRA_TARGETS += icon_asset
}

SOURCES += src/sidebar.cpp
HEADERS += src/sidebar.h

SOURCES += src/library_maintenance.cpp
HEADERS += src/library_maintenance.h

SOURCES += src/activity_log.cpp src/activity_log_data.cpp
HEADERS += src/activity_log.h

HEADERS += native/device_installer.h
