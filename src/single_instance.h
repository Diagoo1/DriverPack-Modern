#ifndef SDIOMODERN_SINGLE_INSTANCE_H
#define SDIOMODERN_SINGLE_INSTANCE_H
#include <QtGlobal>
class QWidget;
class SingleInstance {
public:
    explicit SingleInstance(const wchar_t *name=0);
    ~SingleInstance();
    bool alreadyRunning()const{return m_existing;}
    bool available()const{return m_available;}
    void registerWindow(QWidget *window);
    void activateExisting();
private:
    void *m_handle;
    bool m_existing,m_available;
    Q_DISABLE_COPY(SingleInstance)
};
#endif
