#ifndef SDIO_WINDOW_MOTION_H
#define SDIO_WINDOW_MOTION_H
#include <QObject>
class QWidget;
// Delegate transitions to the window manager. No screenshot overlays, hidden
// live windows, per-frame geometry changes or opacity manipulation.
class WindowMotion : public QObject {
public:
    explicit WindowMotion(QWidget *window);
    void minimize();
    void toggleMaximized();
    void restoredFromMinimized();
    bool busy()const{return m_busy;}
private:
    QWidget *m_window;
    bool m_busy=false;
};
#endif
