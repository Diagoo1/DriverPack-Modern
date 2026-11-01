#ifndef TAREK_SADEK_ORIGIN_LANGUAGES_H
#define TAREK_SADEK_ORIGIN_LANGUAGES_H
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>

// Reads *original* SDIO DriverPack translation files from the local langs folder.
// No downloaded translations or pretend translations are synthesized.
// Qt-specific texts without an upstream equivalent deliberately fall back to English.
class OriginLanguages {
public:
    static QString folder(const QString &appDir);
    static QList<QPair<QString,QString> > available(const QString &appDir);
    static QString text(const QString &appDir,const QString &language,const QString &key);
};
#endif
