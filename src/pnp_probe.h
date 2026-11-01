#ifndef TAREK_SADEK_PNP_PROBE_H
#define TAREK_SADEK_PNP_PROBE_H
#include <QString>
#include <QMap>
// Windows SetupAPI inventory, independent of SDIO matching selection.
// Does not modify devices, registry, or drivers.
class PnpProbe {
public:
    static QMap<QString,QString> presentDevices();
};
#endif
