#include "ui_font.h"
#include "ui_elements.h"
#include "theme.h"
#include "ui_language.h"
#include "vector_icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QTimer>
#include <QApplication>
#include <QDesktopWidget>
#include <QLinearGradient>
#include <QFontMetrics>
#include <QEvent>
#include <QtMath>
#include <QPolygon>

QPixmap makeBrandMark(int diameter, bool light)
{
    QPixmap pix(diameter, diameter);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor stroke = light ? QColor("#D8F8F7") : QColor("#20C6BB");
    p.setPen(QPen(stroke, qMax(2.0, diameter * 0.075), Qt::SolidLine, Qt::RoundCap));
    QRectF outer(4, 4, diameter-8, diameter-8);
    p.drawRoundedRect(outer, diameter*0.18, diameter*0.18);
    p.drawLine(QPointF(diameter*0.29,diameter*0.53),QPointF(diameter*0.71,diameter*0.53));
    p.drawLine(QPointF(diameter*0.5,diameter*0.28),QPointF(diameter*0.5,diameter*0.72));
    p.setBrush(stroke);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(diameter*0.5,diameter*0.53),diameter*0.055,diameter*0.055);
    return pix;
}

QIcon makeToolbarIcon(int type, const QColor &color)
{
    return VectorIcons::icon(type==0 ? QStringLiteral("refresh") : QStringLiteral("menu"),color,26);
}
QIcon makeNavIcon(int type, const QColor &color)
{
    const char *symbols[]={"wrench","activity","layers","settings"};
    const int i=qBound(0,type,3);
    return VectorIcons::icon(QString::fromLatin1(symbols[i]),color,32);
}

GradientButton::GradientButton(const QString &text, QWidget *parent)
  : QPushButton(text, parent), m_hover(0.0), m_hoverAnim(new QPropertyAnimation(this,"hoverLevel",this)),
    m_textAnim(new QPropertyAnimation(this,"textBlend",this)),m_textBlend(1.0),
    m_iconName(QStringLiteral("scan")),m_motionEnabled(true)
{
    setObjectName(QStringLiteral("gradientButton"));
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(68);
    setMinimumWidth(228);
    setFocusPolicy(Qt::StrongFocus);
    m_hoverAnim->setDuration(165);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_textAnim->setDuration(210);
    m_textAnim->setEasingCurve(QEasingCurve::OutCubic);
}
void GradientButton::setTextBlend(qreal value) { m_textBlend=value; update(); }
void GradientButton::setMotionEnabled(bool enabled)
{
    m_motionEnabled=enabled;
    if(!enabled){
        m_hoverAnim->stop();
        m_textAnim->stop();
        setTextBlend(1.0);
        setHoverLevel(underMouse()?1.0:0.0);
    }
}
void GradientButton::setActionState(const QString &title,const QString &iconName)
{
    const QString localizedTitle=UiLanguage::text(title);
    if(text()==localizedTitle && m_iconName==iconName) return;
    m_textAnim->stop();
    m_oldText=text();m_oldIcon=m_iconName;
    m_iconName=iconName;
    QPushButton::setText(localizedTitle);
    if(m_motionEnabled && !m_oldText.isEmpty()){
        setTextBlend(0.0);
        m_textAnim->setStartValue(0.0);m_textAnim->setEndValue(1.0);
        m_textAnim->start();
    }else setTextBlend(1.0);
}
void GradientButton::setHoverLevel(qreal x) { m_hover = x; update(); }
void GradientButton::enterEvent(QEvent *event)
{
    m_hoverAnim->stop();
    if(m_motionEnabled){
        m_hoverAnim->setStartValue(m_hover);
        m_hoverAnim->setEndValue(1.0);
        m_hoverAnim->start();
    }else setHoverLevel(1.0);
    QPushButton::enterEvent(event);
}
void GradientButton::leaveEvent(QEvent *event)
{
    m_hoverAnim->stop();
    if(m_motionEnabled){
        m_hoverAnim->setStartValue(m_hover);
        m_hoverAnim->setEndValue(0.0);
        m_hoverAnim->start();
    }else setHoverLevel(0.0);
    QPushButton::leaveEvent(event);
}
void GradientButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing,true);
    QRectF shadow = rect().adjusted(3,5,-3,-1);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,63,73,isEnabled() ? 32 : 12));
    p.drawRoundedRect(shadow,16,16);
    QRectF r=rect().adjusted(2,1,-3,-5);
    QLinearGradient gradient(r.topLeft(),r.topRight());
    const int hoverLight=100+qRound(6*m_hover);
    gradient.setColorAt(0,isEnabled()?Theme::color("heroStart").lighter(hoverLight):QColor("#718A8C"));
    gradient.setColorAt(.55,isEnabled()?Theme::color("heroMiddle").lighter(hoverLight):QColor("#859A9B"));
    gradient.setColorAt(1,isEnabled()?Theme::color("heroEnd").lighter(hoverLight):QColor("#A2B0AE"));
    p.setBrush(gradient);
    p.drawRoundedRect(r,14,14);
    QFont f=font(); f.setFamily(UiFont::family()); f.setPointSize(15); f.setWeight(QFont::Medium);
    p.setFont(f);p.setPen(Qt::white);
    const bool rtl=layoutDirection()==Qt::RightToLeft;
    const QRect iconRect(rtl?width()-56:26,qRound(r.center().y())-15,30,30);
    const QRect textRect(rtl?27:65,1,qMax(50,width()-92),height()-6);
    if(m_textBlend<1.0 && !m_oldText.isEmpty()){
        p.setOpacity(1.0-m_textBlend);
        VectorIcons::paint(&p,m_oldIcon,iconRect,QColor("#E6FFFB"));
        p.drawText(textRect,Qt::AlignVCenter|Qt::AlignHCenter,m_oldText);
    }
    p.setOpacity(m_textBlend);
    VectorIcons::paint(&p,m_iconName,iconRect,QColor("#E6FFFB"));
    p.drawText(textRect,Qt::AlignVCenter|Qt::AlignHCenter,text());
    p.setOpacity(1.0);
}

ToggleSwitch::ToggleSwitch(QWidget *parent)
  : QAbstractButton(parent), m_position(0.0), m_animations(true),
    m_dark(false),m_anim(new QPropertyAnimation(this,"position",this))
{
    setCheckable(true);
    setFixedSize(50,27);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    m_anim->setDuration(175);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(this,&QAbstractButton::toggled,this,[this](bool value) {
        m_anim->stop();
        if (m_animations) {
            m_anim->setStartValue(m_position);
            m_anim->setEndValue(value ? 1.0 : 0.0);
            m_anim->start();
        } else setPosition(value ? 1.0 : 0.0);
    });
}
QSize ToggleSwitch::sizeHint() const { return QSize(50,27); }
void ToggleSwitch::setPosition(qreal p) { m_position = p; update(); }
void ToggleSwitch::setMotionEnabled(bool enabled)
{
    m_animations=enabled;
    if(!enabled){
        m_anim->stop();
        setPosition(isChecked()?1.0:0.0);
    }
}
void ToggleSwitch::setDark(bool dark) {m_dark=dark;update();}
void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing,true);
    const QRectF track(2.0,3.5,46.0,20.0);
    const QColor trackOff=m_dark?QColor("#4B6372"):QColor("#E6EDEF");
    const QColor trackOn("#00B4A6");
    const qreal t=qBound(0.0,m_position,1.0);
    QColor fill=QColor::fromRgbF(trackOff.redF()+(trackOn.redF()-trackOff.redF())*t,
                                trackOff.greenF()+(trackOn.greenF()-trackOff.greenF())*t,
                                trackOff.blueF()+(trackOn.blueF()-trackOff.blueF())*t);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(track,10,10);
    const qreal cx=12.5+25.0*t;
    p.setPen(QPen(QColor(0,0,0,17),.8));
    p.setBrush(QColor("#FFFFFF"));
    p.drawEllipse(QRectF(cx-8.1,5.5,16.2,16.2));

}

Splash::Splash(QWidget *parent) : QWidget(parent), m_progress(0)
{
    setWindowFlags(Qt::FramelessWindowHint|Qt::SplashScreen);
    setAttribute(Qt::WA_TranslucentBackground,true);
    setFixedSize(850,520);
}
void Splash::setProgress(int x) { m_progress = x; update(); }
void Splash::begin()
{
    const QRect screen = QApplication::desktop()->availableGeometry(this);
    move(screen.center()-rect().center());
    show();
    QPropertyAnimation *a = new QPropertyAnimation(this,"progress",this);
    a->setStartValue(0); a->setEndValue(88); a->setDuration(1250);
    a->setEasingCurve(QEasingCurve::InOutCubic);
    connect(a,&QPropertyAnimation::finished,this,&Splash::finished);
    a->start(QAbstractAnimation::DeleteWhenStopped);
}
void Splash::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing,true);
    QLinearGradient g(rect().topLeft(),rect().topRight());
    g.setColorAt(0,QColor("#064D73")); g.setColorAt(1,QColor("#0BCBB9"));
    p.setPen(Qt::NoPen);p.setBrush(g);p.drawRoundedRect(QRectF(rect()),24,24);
    QPixmap logo(QStringLiteral(":/sdio/assets/driverpack-emblem.png"));
    const qreal dpr=devicePixelRatioF();
    logo=logo.scaled(qRound(152*dpr),qRound(152*dpr),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    logo.setDevicePixelRatio(dpr);
    p.drawPixmap((width()-152)/2,84,logo);
    QFont title = font(); title.setPointSize(34); title.setWeight(QFont::DemiBold);
    p.setFont(title); p.setPen(QColor("#E8FAFA"));
    p.drawText(QRect(0,255,width(),65),Qt::AlignCenter,UiLanguage::language()==QStringLiteral("arabic")?QString::fromUtf8("درايفر باك مودرن"):QStringLiteral("DriverPack Modern"));
    QFont version=font();version.setPixelSize(14);p.setFont(version);p.setPen(QColor("#C7F1EF"));p.drawText(QRect(0,329,width(),28),Qt::AlignCenter,QStringLiteral("Beta 0.22.9"));
    QFont cap = font(); cap.setPointSize(13); p.setFont(cap);
    p.setPen(QColor("#E5F8F8"));
    p.drawText(QRect(0,385,width(),25),Qt::AlignCenter,UiLanguage::text(QStringLiteral("Preparing your workspace...")));
    const QRect bar(194,436,462,9);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#08708C")); p.drawRoundedRect(bar,4,4);
    QLinearGradient progress(bar.topLeft(),bar.topRight());progress.setColorAt(0,QColor("#5DE8EE"));progress.setColorAt(1,QColor("#4BE3A8"));p.setBrush(progress);
    p.drawRoundedRect(QRect(bar.x(),bar.y(),qMax(10,bar.width()*m_progress/100),bar.height()),4,4);
    QFont credit=font();credit.setPixelSize(13);credit.setWeight(QFont::Medium);p.setFont(credit);p.setPen(QColor("#DCF8F3"));p.drawText(QRect(0,467,width(),24),Qt::AlignCenter,UiLanguage::language()==QStringLiteral("arabic")?QString::fromUtf8("تصميم وتطوير الواجهة: طارق صادق · جميع الحقوق محفوظة © 2026"):QStringLiteral("Interface design & development: Tarek Sadek · © 2026"));
}
