#include "ui_font.h"
#include "sidebar.h"
#include "appearance.h"
#include "theme.h"
#include "vector_icons.h"
#include "origin_languages.h"
#include <QCoreApplication>
#include <QPainter>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QFontMetrics>
#include <QEvent>
#include <QPainterPath>
#include <QApplication>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>

SidebarItem::SidebarItem(const QString &symbol,QWidget *parent)
  : QAbstractButton(parent),m_iconName(symbol),m_compact(false),m_motion(true),
    m_hover(0),m_animation(new QPropertyAnimation(this,"hoverAmount",this))
{
    setCheckable(true);
    setAutoExclusive(false);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(66);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    m_animation->setDuration(150);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
}
void SidebarItem::setCompact(bool compact) { m_compact=compact; update(); }
void SidebarItem::nextCheckState(){if(!property("sharedNavigation").toBool())QAbstractButton::nextCheckState();}
void SidebarItem::setMotionEnabled(bool enabled)
{
    m_motion=enabled;
    if(!enabled){m_animation->stop();setHoverAmount(underMouse()?1.0:0.0);}
}
void SidebarItem::setHoverAmount(qreal h) { m_hover=h; update(); }
void SidebarItem::enterEvent(QEvent *e)
{
    m_animation->stop();
    if (m_motion) {m_animation->setStartValue(m_hover);m_animation->setEndValue(1.0);m_animation->start();}
    else setHoverAmount(1.0);
    QAbstractButton::enterEvent(e);
}
void SidebarItem::leaveEvent(QEvent *e)
{
    m_animation->stop();
    if (m_motion) {m_animation->setStartValue(m_hover);m_animation->setEndValue(0.0);m_animation->start();}
    else setHoverAmount(0.0);
    QAbstractButton::leaveEvent(e);
}
void SidebarItem::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing,true);
    const bool active=isChecked(),rtl=layoutDirection()==Qt::RightToLeft;
    const bool shared=property("sharedNavigation").toBool();
    if (active && !shared) {
        p.setPen(Qt::NoPen);p.setBrush(Theme::color("sidebarMiddle").lighter(145));p.drawRoundedRect(rect().adjusted(8,4,-8,-4),12,12);
        p.setBrush(Theme::color("accentSoft"));
        p.drawRoundedRect(QRectF(rtl?width()-6:2,(height()-32)/2.0,4,32),2,2);
    }
    if (m_hover>0.001 && !shared) {
        QColor hover=Theme::color("accentSoft");hover.setAlphaF(m_hover * (active ? 0.18 : 0.29));
        p.setPen(Qt::NoPen);p.setBrush(hover);p.drawRoundedRect(rect().adjusted(8,4,-8,-4),12,12);
    }
    if (isDown() && !shared) {p.setPen(Qt::NoPen);p.setBrush(QColor(1,22,41,42));p.drawRoundedRect(rect().adjusted(8,4,-8,-4),12,12);}
    const int iconSize=24;
    const qreal openness=qBound(0.0,(width()-84)/140.0,1.0);
    const int logicalX=qRound(30-5*openness);
    const int ix=rtl?width()-logicalX-iconSize:logicalX;
    const int iy=(height()-iconSize)/2;
    const QColor ink=active ? QColor("#FFFFFF") : QColor("#B2D1E2");
    VectorIcons::paint(&p,m_iconName,QRectF(ix,iy,iconSize,iconSize),ink);
    const qreal labelOpacity=qBound(0.0,(width()-100)/100.0,1.0);
    if (labelOpacity>.001) {
        p.setOpacity(labelOpacity);
        QFont f=font(); f.setFamily(UiFont::family()); f.setPointSize(10);
        f.setWeight(active ? QFont::DemiBold : QFont::Normal);
        p.setFont(f);
        p.setPen(ink);
        const QRect textRect(rtl?12:ix+iconSize+15,0,qMax(8,width()-(logicalX+iconSize+22)),height());
        const QString label=QFontMetrics(f).elidedText(text(),Qt::ElideRight,textRect.width());
        p.drawText(textRect,Qt::AlignAbsolute|(rtl?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter,label);
    }

}

Sidebar::Sidebar(QWidget *parent)
  : QWidget(parent),m_slide(new QPropertyAnimation(this,"menuWidth",this)),
    m_layout(new QVBoxLayout(this)),m_footer(0),m_collapsed(false),m_arabicEnabled(false),
    m_animationsEnabled(true),m_labelsHidden(false),m_current(0),
    m_activeY(-1),m_hoverY(-1),m_hoverOpacity(0),m_hoverButton(0),
    m_activeMove(new QPropertyAnimation(this,"activeY",this)),
    m_hoverMove(new QPropertyAnimation(this,"hoverY",this)),
    m_hoverFade(new QPropertyAnimation(this,"hoverOpacity",this))
{
    m_activeMove->setDuration(200);m_hoverMove->setDuration(170);m_hoverFade->setDuration(120);
    m_activeMove->setEasingCurve(QEasingCurve::OutCubic);m_hoverMove->setEasingCurve(QEasingCurve::OutCubic);m_hoverFade->setEasingCurve(QEasingCurve::OutCubic);
    setObjectName(QStringLiteral("sidebar"));
    setFixedWidth(250);
    m_layout->setContentsMargins(0,18,0,10);
    m_layout->setSpacing(0);
    makeButton(0,QStringLiteral("Drivers"),QString::fromUtf8("التعريفات"),QStringLiteral("wrench"));
    makeButton(1,QStringLiteral("Software"),QString::fromUtf8("البرامج"),QStringLiteral("apps"));
    makeButton(2,QStringLiteral("Security"),QString::fromUtf8("الحماية"),QStringLiteral("shield"));
    makeButton(3,QStringLiteral("Diagnostics"),QString::fromUtf8("تشخيص الجهاز"),QStringLiteral("activity"));
    makeButton(4,QStringLiteral("DriverPack Offline"),QString::fromUtf8("درايفر باك أوفلاين"),QStringLiteral("layers"));
    m_layout->addStretch(1);
    QFrame *sep=new QFrame(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QStringLiteral("background:#276A8D;border:none;"));
    m_layout->addWidget(sep);
    makeButton(5,QStringLiteral("Settings"),QString::fromUtf8("الإعدادات"),QStringLiteral("settings"));
    makeButton(6,QStringLiteral("About"),QString::fromUtf8("حول البرنامج"),QStringLiteral("info"));
    m_footer=new QLabel(QStringLiteral("UI © 2026 Tarek Sadek"),this);
    m_footer->setObjectName(QStringLiteral("sidebarFooter"));
    m_footer->setAlignment(Qt::AlignCenter);
    m_footer->setFixedHeight(31);
    m_layout->addWidget(m_footer);
    m_slide->setDuration(270);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    updateActive();
}

SidebarItem *Sidebar::makeButton(int index,const QString &en,const QString &ar,const QString &symbol)
{
    SidebarItem *button=new SidebarItem(symbol,this);
    button->setText(en);
    button->setProperty("pageIndex",index);
    button->setProperty("sharedNavigation",true);
    button->installEventFilter(this);
    button->setToolTip(en);
    m_buttons.append(button);
    m_english.append(en);
    m_arabic.append(ar);
    connect(button,&SidebarItem::clicked,this,[this,index](){emit pageRequested(index);});
    m_layout->addWidget(button);
    return button;
}

void Sidebar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    QLinearGradient surface(0,0,0,height());
    surface.setColorAt(0,Theme::color("sidebarStart"));
    surface.setColorAt(.55,Theme::color("sidebarMiddle"));
    surface.setColorAt(1,Theme::color("sidebarEnd"));
    p.fillRect(rect(),surface);
    if(m_activeY>=0){
        const QRectF active(8,m_activeY+4,width()-16,58);QPainterPath activePath;activePath.addRoundedRect(active,12,12);
        QColor fill=Theme::color("sidebarMiddle").lighter(145);for(SidebarItem *button:m_buttons)if(button->isChecked() && button->isDown()){fill=fill.darker(108);break;}
        p.fillPath(activePath,fill);
        if(m_hoverY>=0 && m_hoverOpacity>.001){QPainterPath hoverPath;hoverPath.addRoundedRect(QRectF(8,m_hoverY+4,width()-16,58),12,12);QColor hover(Qt::white);hover.setAlpha(qRound(24*m_hoverOpacity));p.fillPath(hoverPath.subtracted(activePath),hover);}
        p.setBrush(Theme::color("accentSoft"));p.drawRoundedRect(QRectF(layoutDirection()==Qt::RightToLeft?width()-6:2,m_activeY+17,4,32),2,2);
    }
    // The top-level window paints the sidebar surface once at the outer edge.
    p.setPen(QColor(255,255,255,18));
    const int x=layoutDirection()==Qt::RightToLeft?0:width()-1;
    p.drawLine(x,0,x,height());
}
bool Sidebar::motionEnabled()const{return m_animationsEnabled && !qApp->property("reduceMotion").toBool() && isVisible();}
void Sidebar::animatePosition(QPropertyAnimation *animation,qreal from,qreal to){
    animation->stop();
    if(!motionEnabled() || from<0 || qAbs(from-to)<.1){animation->targetObject()->setProperty(animation->propertyName().constData(),to);return;}
    animation->setStartValue(from);animation->setEndValue(to);animation->start();
}
void Sidebar::moveHover(SidebarItem *button){
    m_hoverButton=button;m_hoverFade->stop();
    if(button){if(m_hoverOpacity<.01)setHoverY(button->y());animatePosition(m_hoverMove,m_hoverY,button->y());}
    else m_hoverMove->stop();
    if(!motionEnabled()){setHoverOpacity(button?1:0);return;}
    m_hoverFade->setStartValue(m_hoverOpacity);m_hoverFade->setEndValue(button?1.0:0.0);m_hoverFade->start();
}
bool Sidebar::eventFilter(QObject *watched,QEvent *event){
    SidebarItem *button=qobject_cast<SidebarItem*>(watched);
    if(button && button->isEnabled()){
        if(event->type()==QEvent::Enter)moveHover(button);
        else if(event->type()==QEvent::Leave && m_hoverButton==button)moveHover(0);
        else if(event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::KeyPress || event->type()==QEvent::KeyRelease)update();
    }
    return QWidget::eventFilter(watched,event);
}
void Sidebar::syncHighlights(){
    m_activeMove->stop();m_hoverMove->stop();
    for(SidebarItem *button:m_buttons)if(button->property("pageIndex").toInt()==m_current){setActiveY(button->y());break;}
    if(m_hoverButton)setHoverY(m_hoverButton->y());
}
void Sidebar::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);m_layout->activate();syncHighlights();}
void Sidebar::showEvent(QShowEvent *event){QWidget::showEvent(event);m_layout->activate();syncHighlights();}
void Sidebar::hideEvent(QHideEvent *event){m_activeMove->stop();m_hoverMove->stop();m_hoverFade->stop();m_hoverButton=0;setHoverOpacity(0);QWidget::hideEvent(event);}
int Sidebar::menuWidth() const {return width();}
void Sidebar::setMenuWidth(int value)
{
    setFixedWidth(value);
    const bool compact=value<144;
    if (compact!=m_labelsHidden) {
        m_labelsHidden=compact;
        if (m_footer) m_footer->setVisible(!compact);
        refreshLabels();
    }
    m_layout->activate();
    syncHighlights();
}
void Sidebar::toggleCollapsed()
{
    m_collapsed=!m_collapsed;
    m_slide->stop();
    if (m_animationsEnabled) {
        m_slide->setStartValue(width());
        m_slide->setEndValue(m_collapsed ? 84 : 250);
        m_slide->start();
    } else setMenuWidth(m_collapsed ? 84 : 250);
}
void Sidebar::setAnimations(bool enabled)
{
    m_animationsEnabled=enabled;
    if(!enabled){
        m_slide->stop();
        setMenuWidth(m_collapsed?84:250);
        m_hoverFade->stop();syncHighlights();setHoverOpacity(m_hoverButton?1:0);
    }
    for (int i=0;i<m_buttons.size();++i) m_buttons.at(i)->setMotionEnabled(enabled);
}
void Sidebar::setCurrentPage(int page)
{m_current=page;updateActive();}
void Sidebar::setArabic(bool arabic)
{m_arabicEnabled=arabic;m_footer->setText(arabic?QString::fromUtf8("واجهة طارق صادق © 2026"):QStringLiteral("UI © 2026 Tarek Sadek"));m_footer->setProperty("skipTranslation",true);refreshLabels();}
void Sidebar::applyOriginLanguage(const QString &directory,const QString &code)
{
    if(code==QStringLiteral("english") || code==QStringLiteral("arabic"))return;
    // Don't replace a page title with an unrelated upstream label. Only map
    // exact shared concepts: installation, diagnostics and settings.
    for(int i=0;i<m_buttons.size();++i){
        const int page=m_buttons.at(i)->property("pageIndex").toInt();
        QString key;
        if(page==3)key=QStringLiteral("STR_SHOW_SYSINFO");
        else if(page==5)key=QStringLiteral("STR_OPTIONS");
        if(key.isEmpty())continue;
        const QString text=OriginLanguages::text(directory,code,key);
        if(text.isEmpty())continue;
        m_buttons[i]->setText(text);m_buttons[i]->setToolTip(text);
    }
}
void Sidebar::refreshLabels()
{
    for (int i=0;i<m_buttons.size();++i) {
        const QString label=m_arabicEnabled?m_arabic.at(i):m_english.at(i);
        SidebarItem *button=m_buttons.at(i);
        button->setText(label);
        button->setToolTip(label);
        button->setCompact(m_labelsHidden);
    }
}
void Sidebar::updateActive()
{
    SidebarItem *active=0;
    for (int i=0;i<m_buttons.size();++i) {
        m_buttons.at(i)->setChecked(m_buttons.at(i)->property("pageIndex").toInt()==m_current);
        m_buttons.at(i)->update();
        if(m_buttons.at(i)->isChecked())active=m_buttons.at(i);
    }
    if(active)animatePosition(m_activeMove,m_activeY,active->y());
}

void Sidebar::setInstallationAvailable(bool enabled)
{
    for(int i=0;i<m_buttons.size();++i)
        if(m_buttons.at(i)->property("pageIndex").toInt()==4)
            m_buttons.at(i)->setVisible(enabled);
}
