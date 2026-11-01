#ifndef SDIO_UI_FONT_H
#define SDIO_UI_FONT_H
#include <QString>
namespace UiFont {
QString family();
void applyLanguage(const QString &language);
bool embeddedFontsAvailable();
}
#endif
