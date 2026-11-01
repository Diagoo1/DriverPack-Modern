// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_MODERN_DEVICES_H
#define DRIVERPACK_MODERN_DEVICES_H
#include "origin_api.h"
namespace driverpack {struct Target{int major=0,minor=0,build=0,architecture=32;};Target currentTarget();std::vector<sdiomodern::Device> presentDevices();}
#endif
