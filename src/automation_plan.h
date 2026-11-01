#ifndef SDIO_AUTOMATION_PLAN_H
#define SDIO_AUTOMATION_PLAN_H
#include <QJsonObject>
#include "driver_catalog.h"
QJsonObject automationPlan(const DriverCatalog&,bool restore);
sdiomodern::InstallPlan resolveAutomationPlan(const sdiomodern::Snapshot&,const QJsonObject&,bool accepted);
int runAutomationReport(int argc,char **argv);
#endif
