#include "theme.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtMath>
#include <stdexcept>

namespace Theme {
static QJsonObject active;
static void fail(const char *message){throw std::runtime_error(message);}
QJsonObject defaultDocument(){
    static const QByteArray json=R"JSON({"schema":1,"id":"classic-teal","name":"Classic Teal","author":"Tarek Sadek","light":{"background":"#F6F7F7","surface":"#FFFFFF","elevated":"#EEF7F8","text":"#233C4D","muted":"#6C8190","border":"#E6ECEE","scrollbar":"#B8CBD4","link":"#007C74"},"dark":{"background":"#142735","surface":"#1D3646","elevated":"#244457","text":"#F3F9FB","muted":"#B5C7D2","border":"#375263","scrollbar":"#638697","link":"#73DED2"},"colors":{"accent":"#00857D","accentHover":"#006A64","accentSoft":"#BDF7EF","headerStart":"#075078","headerMiddle":"#075266","headerEnd":"#08605B","sidebarStart":"#064E7B","sidebarMiddle":"#064B74","sidebarEnd":"#063F68","heroStart":"#084E76","heroMiddle":"#076477","heroEnd":"#087E74"}})JSON";
    return QJsonDocument::fromJson(json).object();
}
QJsonObject document(){return active.isEmpty()?defaultDocument():active;}
static double luminance(const QColor &c){auto linear=[](double n){n/=255.;return n<=.04045?n/12.92:qPow((n+.055)/1.055,2.4);};return .2126*linear(c.red())+.7152*linear(c.green())+.0722*linear(c.blue());}
static double contrast(const QColor &a,const QColor &b){const double x=luminance(a),y=luminance(b);return (qMax(x,y)+.05)/(qMin(x,y)+.05);}
void validate(const QJsonObject &pack){
    if(pack.value(QStringLiteral("schema")).toInt()!=1)fail("Unsupported theme schema.");
    static const QRegularExpression id(QStringLiteral("^[a-z0-9][a-z0-9-]{0,47}$")),hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    if(!id.match(pack.value(QStringLiteral("id")).toString()).hasMatch())fail("Use a short lowercase theme ID.");
    for(const char *key:{"name","author"}){const QString value=pack.value(QLatin1String(key)).toString();if(value.isEmpty() || value.size()>80 || value.contains(QLatin1Char('<')) || value.contains(QLatin1Char('>')) || value.contains(QLatin1Char('\n')))fail("Invalid theme name or author.");}
    const QJsonObject defaults=defaultDocument();
    for(const char *group:{"light","dark","colors"}){
        const QJsonObject values=pack.value(QLatin1String(group)).toObject(),required=defaults.value(QLatin1String(group)).toObject();
        if(values.size()!=required.size())fail("Theme palette keys do not match the documented schema.");
        for(auto it=required.constBegin();it!=required.constEnd();++it)if(!hex.match(values.value(it.key()).toString()).hasMatch())fail("Theme colors must be opaque #RRGGBB values.");
        if(QLatin1String(group)!=QLatin1String("colors")){
            const QColor text(values.value(QStringLiteral("text")).toString()),muted(values.value(QStringLiteral("muted")).toString()),link(values.value(QStringLiteral("link")).toString());
            for(const char *surface:{"background","surface","elevated"}){const QColor background(values.value(QLatin1String(surface)).toString());if(contrast(text,background)<4.5 || contrast(muted,background)<3 || contrast(link,background)<4.5)fail("Theme text contrast is too low.");}
        }
    }
    const QJsonObject colors=pack.value(QStringLiteral("colors")).toObject();
    for(const char *role:{"accent","accentHover","headerStart","headerMiddle","headerEnd","heroStart","heroMiddle","heroEnd"})if(contrast(Qt::white,QColor(colors.value(QLatin1String(role)).toString()))<4.5)fail("Theme header or button contrast is too low.");
}
QJsonObject readPack(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>65536)fail("Cannot read this bounded theme file.");QJsonParseError error;const QJsonDocument data=QJsonDocument::fromJson(file.readAll(),&error);if(error.error!=QJsonParseError::NoError || !data.isObject())fail("Invalid theme JSON.");validate(data.object());return data.object();}
void setDocument(const QJsonObject &pack){if(pack.isEmpty()){active=QJsonObject();return;}validate(pack);active=pack;}
void writePack(const QString &path,const QJsonObject &pack){validate(pack);QSaveFile file(path);const QByteArray data=QJsonDocument(pack).toJson();if(!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit())fail("Cannot save the theme pack.");}
QColor color(const char *role,bool dark){const QJsonObject pack=document();const QString key=QLatin1String(role);const QJsonObject palette=pack.value(dark?QStringLiteral("dark"):QStringLiteral("light")).toObject();const QString value=palette.contains(key)?palette.value(key).toString():pack.value(QStringLiteral("colors")).toObject().value(key).toString();return QColor(value);}
}
