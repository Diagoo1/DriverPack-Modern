// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_DIAGNOSTIC_SESSIONS_H
#define DRIVERPACK_DIAGNOSTIC_SESSIONS_H
#include <QString>
namespace DiagnosticSessions {
QString create(const QString &root);
// Only marked, directly contained session folders containing known log files.
void prune(const QString &root,const QString &current,int normalLimit=5,int crashLimit=3,qint64 budget=64*1024*1024);
int clearFinished(const QString &root,const QString &current); // retains newest crash
}
#endif
