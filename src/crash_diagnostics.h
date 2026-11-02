// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_CRASH_DIAGNOSTICS_H
#define DRIVERPACK_CRASH_DIAGNOSTICS_H
#include <QString>
namespace CrashDiagnostics {
// Local only. Also works when the library/EXE is on read-only media.
bool initialize(const QString &directory);
QString directory();
void checkpoint(const QString &message);
void shutdown();
bool clearLogs(int *removedSessions=0); // call only while engine/downloads are idle
void testCrash();
}
#endif
