// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_RESTORE_STATUS_H
#define DRIVERPACK_RESTORE_STATUS_H
#include <cstdint>
namespace driverpack {
inline bool allowDisabledRestoreBypass(bool explicitlyConfirmed,std::uint32_t error) {
    return explicitlyConfirmed && error==1058;
}
inline std::uint32_t restoreStatus(bool returned,std::uint32_t status,std::uint32_t lastError) {
    if(status)return status;
    return returned?0:(lastError?lastError:31); // ERROR_GEN_FAILURE, never report failed calls as success.
}
}
#endif
