#ifndef SDIOMODERN_SYSTEM_PROFILE_H
#define SDIOMODERN_SYSTEM_PROFILE_H

#include <QMap>
#include <QString>
#include <QVector>
#include "device_types.h"

// Read-only, local system profile. No WMI, network, shell calls or driver writes.
// Values come from Windows Registry, documented Win32 APIs and already-scanned PnP devices.
// Missing information remains explicitly unavailable; demo PnP rows are never treated as hardware.
class SystemProfile
{
public:
    static QMap<QString,QString> collect(const QVector<DeviceInfo> &devices,bool demo);
    static QString displayValue(const QString &key,const QString &raw);
};

#endif
