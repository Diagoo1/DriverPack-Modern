#ifndef SDIO_UI_LANGUAGE_H
#define SDIO_UI_LANGUAGE_H
#include <QString>
#include <QList>
#include <QPair>
namespace UiLanguage {
void install();
void setLanguage(const QString &language);
QString language();
bool isRtl();
QString text(const QString &english,const QString &arabic=QString());
QList<QPair<QString,QString> > available();
void refresh();
}
#endif
