#pragma once
#include <QStringList>
namespace LibraryMaintenance
{
QStringList preview(const QString &library);
void clean(const QString &library);
int dialog(const QString &library);
} // namespace LibraryMaintenance
