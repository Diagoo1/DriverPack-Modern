#ifndef SDIOMODERN_DEVICE_SCANNER_H
#define SDIOMODERN_DEVICE_SCANNER_H

#include <QThread>
#include "device_types.h"

class DeviceScanner : public QThread
{
    Q_OBJECT
public:
    explicit DeviceScanner(QObject *parent = 0);
    void setDemo(bool demo);

signals:
    void inventoryReady(const QVector<DeviceInfo> &devices, const QString &message);

protected:
    void run() Q_DECL_OVERRIDE;

private:
    bool m_demo;
};

#endif
