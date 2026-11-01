#include "window_motion.h"
#include <QWidget>
#include <QVariant>
WindowMotion::WindowMotion(QWidget *window):QObject(window),m_window(window){
    setObjectName(QStringLiteral("windowMotion"));
    m_window->setProperty("windowMotionBackend",QStringLiteral("native-window-manager"));
}
void WindowMotion::minimize(){
    if(m_busy)return;m_busy=true;m_window->showMinimized();m_busy=false;
}
void WindowMotion::toggleMaximized(){
    if(m_busy)return;m_busy=true;
    if(m_window->isMaximized())m_window->showNormal();else m_window->showMaximized();
    m_busy=false;
}
void WindowMotion::restoredFromMinimized(){
    // The window manager has already restored it. Never hide or re-capture it.
}
