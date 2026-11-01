#ifndef SDIO_UI_SCALE_H
#define SDIO_UI_SCALE_H
#include <QSize>
#include <QString>
namespace UiScale {
int fitPercent(int requested,const QSize &availableAt100);
void prepareEnvironment();
int effectivePercent();
bool restartAfterParent(int argc,char **argv);
bool administrator();
QString restartElevated();
}
#endif
