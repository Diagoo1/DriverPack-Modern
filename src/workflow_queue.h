#ifndef DRIVERPACK_WORKFLOW_QUEUE_H
#define DRIVERPACK_WORKFLOW_QUEUE_H
#include "../native/origin_api.h"
namespace DriverPackWorkflow {
inline bool offerDisabledRestoreChoice(const sdiomodern::InstallReport &report){
 return report.restoreFailed && report.restoreError==1058 && report.items.empty() && !report.cancelled;
}
inline bool allowSoftwareAfterDrivers(const sdiomodern::InstallReport &report){
 if(report.restoreFailed||report.cancelled||!report.error.empty()||report.items.empty())return false;
 for(const auto &item:report.items)if(item.outcome!=sdiomodern::Outcome::Success)return false;
 return true;
}
}
#endif
