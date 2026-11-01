// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_MODERN_ARCHIVE_H
#define DRIVERPACK_MODERN_ARCHIVE_H
#include <QString>
#include <atomic>
namespace driverpack {void extractDirectory(const QString &archive,const QString &directory,const QString &destination,const std::atomic<bool> &stop);}
#endif
