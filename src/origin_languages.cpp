#include "origin_languages.h"
#include "ui_language.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QHash>
#include <QMap>

QString OriginLanguages::folder(const QString &appDir)
{
    const QString local=QDir(appDir).filePath(QStringLiteral("langs"));
    if(QDir(local).exists())return local;
    const QString source=QDir(appDir).filePath(QStringLiteral("vendor/origin-patched/tools/DriverPack/langs"));
    if(QDir(source).exists())return source;
    return QString();
}
static const QMap<QString,QString> &translations(const QString &path)
{
    // Read once per process. Original SDIO language files are immutable runtime
    // assets and can contain many hundreds of keys; repeated regex scans cause
    // needless delays on settings changes and initial dropdown population.
    static QHash<QString,QMap<QString,QString> > cache;
    if(cache.contains(path))return cache[path];
    QMap<QString,QString> values;
    QFile file(path);
    if(file.open(QIODevice::ReadOnly) && file.size()<=512*1024){
        const QString source=QString::fromUtf8(file.readAll()).remove(QChar(0xFEFF));
        const QRegularExpression regex(QStringLiteral("(?m)^\\s*([A-Z][A-Z0-9_]*)\\s*=\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
        QRegularExpressionMatchIterator match=regex.globalMatch(source);
        while(match.hasNext()){
            QRegularExpressionMatch entry=match.next();
            QString value=entry.captured(2);
            value.replace(QStringLiteral("\\n"),QStringLiteral("\n"));
            value.replace(QStringLiteral("\\\""),QStringLiteral("\""));
            values.insert(entry.captured(1),value.trimmed());
        }
    }
    cache.insert(path,values);
    return cache[path];
}
static QString translated(const QString &path,const QString &key)
{
    return translations(path).value(key);
}
QList<QPair<QString,QString> > OriginLanguages::available(const QString &appDir)
{
    QList<QPair<QString,QString> > list=UiLanguage::available();
    if(!list.isEmpty())return list;
    const QString dir=folder(appDir);
    if(!dir.isEmpty()){
        const QStringList files=QDir(dir).entryList(QStringList()<<QStringLiteral("*.txt"),QDir::Files,QDir::Name);
        for(int i=0;i<files.size();++i){
            const QString code=QFileInfo(files.at(i)).completeBaseName().toLower();
            if(code==QStringLiteral("hebrew"))continue;
            const QString label=translated(QDir(dir).filePath(files.at(i)),QStringLiteral("STR_LANG_NAME"));
            if(!label.isEmpty())list.append(qMakePair(code,label));
        }
    }
    // UI-only builds (before source preparation) still have the two locally maintained UI locales.
    bool hasEnglish=false,hasArabic=false;
    for(int i=0;i<list.size();++i){
        if(list.at(i).first==QStringLiteral("english"))hasEnglish=true;
        if(list.at(i).first==QStringLiteral("arabic"))hasArabic=true;
    }
    if(!hasEnglish)list.prepend(qMakePair(QStringLiteral("english"),QStringLiteral("English")));
    if(!hasArabic)list.append(qMakePair(QStringLiteral("arabic"),QString::fromUtf8("العربية")));
    return list;
}
QString OriginLanguages::text(const QString &appDir,const QString &language,const QString &key)
{
    const QString dir=folder(appDir);
    if(dir.isEmpty())return QString();
    const QString clean=QFileInfo(language).fileName();
    if(clean!=language || clean.contains(QLatin1Char('.')) || clean.contains(QLatin1Char('/')) || clean.contains(QLatin1Char('\\')))
        return QString();
    return translated(QDir(dir).filePath(clean+QStringLiteral(".txt")),key);
}
