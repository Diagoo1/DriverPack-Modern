#ifndef SDIO_ABOUT_PAGE_H
#define SDIO_ABOUT_PAGE_H
#include <QWidget>
class QScrollArea;
QWidget *createSignatureHeart(QWidget *parent);
namespace AboutMotion {qreal heartbeatScale(qint64 elapsedMs);}
class AboutPage : public QWidget {
public:
    explicit AboutPage(bool arabic,QWidget *parent=0);
    void setArabic(bool arabic);
private:
    QScrollArea *m_scroll;
};
#endif
