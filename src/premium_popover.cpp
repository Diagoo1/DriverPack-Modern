#include "appearance.h"
#include "premium_popover.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QApplication>
#include <QDesktopWidget>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPoint>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QScrollArea>
// Opaque popup avoids black DWM borders on legacy Qt/MinGW Windows builds.

// Qt::Popup closes this native popover automatically on outside clicks.
PremiumPopover::PremiumPopover(QWidget *parent)
  : QFrame(parent,Qt::Popup|Qt::FramelessWindowHint|Qt::NoDropShadowWindowHint),m_card(0),m_content(0),m_motion(true),m_dark(false)
{
    setObjectName(QStringLiteral("premiumPopover"));
    setAttribute(Qt::WA_TranslucentBackground,true);
    setAttribute(Qt::WA_StyledBackground,false);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(286);
    setMaximumWidth(366);
    QVBoxLayout *outer=new QVBoxLayout(this);
    outer->setContentsMargins(7,7,7,9);
    outer->setSpacing(0);
    m_card=new QFrame(this);
    m_card->setObjectName(QStringLiteral("premiumPopupCard"));
    m_card->setAttribute(Qt::WA_StyledBackground,true);
    m_content=new QVBoxLayout(m_card);
    m_content->setContentsMargins(13,11,13,11);
    m_content->setSpacing(8);
    QScrollArea *scroll=new QScrollArea(this);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:0;} QScrollArea>QWidget>QWidget{background:transparent;}"));scroll->setWidget(m_card);outer->addWidget(scroll);
}

QLabel *PremiumPopover::addHeading(const QString &text)
{
    QLabel *heading=new QLabel(text,m_card);
    heading->setObjectName(QStringLiteral("popoverSection"));
    heading->setMinimumHeight(27);
    heading->setContentsMargins(6,3,6,4);
    m_content->addWidget(heading);
    return heading;
}

QCheckBox *PremiumPopover::addOption(const QString &text,bool checked)
{
    QCheckBox *option=new QCheckBox(text,m_card);
    option->setObjectName(QStringLiteral("popoverCheck"));
    option->setChecked(checked);
    option->setMinimumHeight(43);
    option->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    option->setCursor(Qt::PointingHandCursor);
    m_content->addWidget(option);
    return option;
}

QPushButton *PremiumPopover::addLink(const QString &text)
{
    QPushButton *link=new ActionButton(text,m_card);
    link->setObjectName(QStringLiteral("popoverLink"));
    link->setCursor(Qt::PointingHandCursor);
    m_content->addWidget(link);
    return link;
}

void PremiumPopover::addDivider()
{
    QFrame *line=new QFrame(m_card);
    line->setObjectName(QStringLiteral("popoverDivider"));
    line->setFixedHeight(1);
    m_content->addWidget(line);
}

void PremiumPopover::openAt(QWidget *anchor)
{
    if(!anchor)return;
    adjustSize();
    const QRect screen=QApplication::desktop()->availableGeometry(anchor);
    setFixedHeight(qMin(m_content->sizeHint().height()+18,screen.height()-24));
    QPoint spot=anchor->mapToGlobal(QPoint(anchor->width()-width(),anchor->height()+8));
    if(spot.x()+width()>screen.right())spot.setX(screen.right()-width()-4);
    if(spot.x()<screen.left()+4)spot.setX(screen.left()+4);
    if(spot.y()+height()>screen.bottom()-4)
        spot.setY(anchor->mapToGlobal(QPoint(0,-height()-8)).y());
    if(spot.y()<screen.top()+4)spot.setY(screen.top()+4);
    move(spot);
    setWindowOpacity(m_motion?0.86:1.0);
    show();raise();setFocus(Qt::PopupFocusReason);
    if(m_motion){
        QPropertyAnimation *fade=new QPropertyAnimation(this,"windowOpacity",this);
        fade->setDuration(140);
        fade->setStartValue(0.86);fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void PremiumPopover::toggleAt(QWidget *anchor)
{if(isVisible())hide();else openAt(anchor);}

void PremiumPopover::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    // Alpha coverage keeps the curve smooth at every DPI; binary masks alias.
}
void PremiumPopover::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing,true);
    const QRectF face=QRectF(rect()).adjusted(6.5,6.5,-6.5,-8.5);paintPopupShadow(&painter,face,16,m_dark);
    painter.setPen(QPen(m_dark?QColor("#3B5360"):QColor("#D9E5E8"),1));
    painter.setBrush(m_dark?QColor("#203B4A"):QColor("#FFFFFF"));
    painter.drawRoundedRect(face,16,16);
}

void PremiumPopover::setCompact()
{
    setProperty("compact",true);m_content->setSpacing(4);
    for(QCheckBox *option:m_card->findChildren<QCheckBox*>())option->setMinimumHeight(30);
    for(QLabel *heading:m_card->findChildren<QLabel*>())heading->setMinimumHeight(24);
}
