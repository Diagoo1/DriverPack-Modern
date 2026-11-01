#ifndef SDIO_RESTART_PROMPT_H
#define SDIO_RESTART_PROMPT_H
#include <QString>
#include <functional>
class QWidget;
// The injectable action lets UI regression tests exercise the entire countdown
// without ever asking Windows to restart.
bool restartPrompt(QWidget *parent,int seconds=60,const std::function<QString()> &action=std::function<QString()>());
#endif
