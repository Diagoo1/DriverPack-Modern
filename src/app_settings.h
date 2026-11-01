#ifndef DRIVERPACK_MODERN_APP_SETTINGS_H
#define DRIVERPACK_MODERN_APP_SETTINGS_H
#include <QSettings>
#include <QObject>
#include <QDir>
inline QSettings *createAppSettings(const QString&,QObject *parent=0,bool=true){const QString fixture=QString::fromUtf8(qgetenv("DRIVERPACK_MODERN_TEST_SETTINGS"));if(!fixture.isEmpty() && QDir::isAbsolutePath(fixture)){QSettings *isolated=new QSettings(fixture,QSettings::IniFormat,parent);isolated->setFallbacksEnabled(false);return isolated;}QSettings *settings=new QSettings(QSettings::NativeFormat,QSettings::UserScope,QStringLiteral("TarekSadek"),QStringLiteral("DriverPackModern"),parent);settings->setFallbacksEnabled(false);return settings;}
#endif
