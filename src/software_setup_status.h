// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_SOFTWARE_SETUP_STATUS_H
#define DRIVERPACK_SOFTWARE_SETUP_STATUS_H
namespace SoftwareSetupStatus {
inline bool success(unsigned long code){return code==0 || code==3010;}
inline bool cancelled(unsigned long code){return code==1602 || code==1223;}
// Other codes, including 2, are installer-specific and cannot prove cancellation.
}
#endif
