#ifndef SDIOMODERN_DEVICE_TYPES_H
#define SDIOMODERN_DEVICE_TYPES_H

#include <QString>
#include <QVector>
#include <QMetaType>

// Read-only inventory. A PnP problem is NOT automatically an available driver update.
struct DeviceInfo
{
    QString name;
    QString manufacturer;
    QString deviceClass;
    QString instanceId;
    QString hardwareId;
    QString driverVersion;
    bool hasProblem;
    unsigned int problemCode;

    DeviceInfo() : hasProblem(false), problemCode(0) {}
};

Q_DECLARE_METATYPE(DeviceInfo)
Q_DECLARE_METATYPE(QVector<DeviceInfo>)

#endif
