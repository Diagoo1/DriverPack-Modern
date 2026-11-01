#include "ui_font.h"
#include <QApplication>
#include <QFontDatabase>
#include <QWidget>
#include <QFont>

namespace UiFont {
static QString selected=QStringLiteral("Satoshi");
static bool loaded=false,available=false;
static void load(){
    if(loaded || !qApp)return;
    loaded=true;available=true;
    const char *files[]={"Satoshi-Regular.ttf","Satoshi-Medium.ttf","Satoshi-Bold.ttf","Cairo-Regular.ttf","Cairo-Medium.ttf","Cairo-Bold.ttf"};
    for(const char *file:files){const int id=QFontDatabase::addApplicationFont(QStringLiteral(":/sdio/assets/fonts/")+QString::fromLatin1(file));available=available && id>=0;}
}
QString family(){load();return selected;}
bool embeddedFontsAvailable(){load();return available;}
void applyLanguage(const QString &language){
    load();selected=(language==QStringLiteral("arabic")||language==QStringLiteral("farsi"))?QStringLiteral("Cairo"):QStringLiteral("Satoshi");
    if(!qApp)return;
    QFont font(selected,10);font.setStyleHint(QFont::SansSerif);font.setStyleStrategy(QFont::PreferQuality);font.setHintingPreference(QFont::PreferFullHinting);qApp->setFont(font);
    // Explicit heading sizes and weights survive a live language switch.
    const auto widgets=QApplication::allWidgets();
    for(QWidget *widget:widgets){QFont face=widget->font();face.setFamily(selected);face.setStyleStrategy(QFont::PreferQuality);face.setHintingPreference(QFont::PreferFullHinting);widget->setFont(face);}
}
}
