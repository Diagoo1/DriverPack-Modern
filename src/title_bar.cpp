#include "ui_font.h"
#include "title_bar.h"
#include "theme.h"
#include "appearance.h"
#include "vector_icons.h"
#include "ui_elements.h"
#include <QToolButton>
#include <QLabel>
#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QFont>
#include <QPainterPath>
#include <QApplication>
#include <QTimer>
#include <QGraphicsOpacityEffect>

TitleBar::TitleBar(QWidget *parent)
  : QWidget(parent),m_menu(0),m_collapse(0),m_refresh(0),m_theme(0),m_min(0),m_max(0),m_close(0),
    m_status(0),m_dragging(false),m_arabic(false),m_maximized(false),m_workflowLocked(false)
{
    setObjectName(QStringLiteral("customTitleBar"));
    setFixedHeight(58);
    setMouseTracking(true);
    QHBoxLayout *l = new QHBoxLayout(this);
    l->setContentsMargins(14,0,10,0);
    l->setSpacing(0);

    QLabel *mark = new QLabel(this);
    mark->setFixedSize(40,40);
    QPixmap logo(QStringLiteral(":/sdio/assets/driverpack-emblem.png"));
    const qreal dpr=qApp->devicePixelRatio();
    logo=logo.scaled(qRound(40*dpr),qRound(40*dpr),Qt::KeepAspectRatio,Qt::SmoothTransformation);logo.setDevicePixelRatio(dpr);
    mark->setPixmap(logo);
    l->addWidget(mark);
    l->addSpacing(13);
    QLabel *brand = new QLabel(QStringLiteral("DriverPack Modern"),this);
    brand->setObjectName(QStringLiteral("headerTitle"));
    brand->setFont(QFont(UiFont::family(),13,QFont::DemiBold));
    brand->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->addWidget(brand);
    l->addSpacing(12);
    QLabel *sub = new QLabel(QStringLiteral("UI by Tarek Sadek"),this);
    sub->setObjectName(QStringLiteral("headerSub"));
    sub->setAttribute(Qt::WA_TransparentForMouseEvents);
    sub->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    l->addWidget(sub);
    l->addStretch(1);

    m_status = new QLabel(QStringLiteral("Your drivers. Clearly organized."),this);
    m_status->setObjectName(QStringLiteral("headerStatus"));
    m_status->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->addWidget(m_status);
    m_status->setMinimumWidth(120);m_status->setMaximumWidth(260);m_status->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
    QGraphicsOpacityEffect *statusFade=new QGraphicsOpacityEffect(m_status);statusFade->setOpacity(1);m_status->setGraphicsEffect(statusFade);
    QTimer *messages=new QTimer(this);messages->setObjectName(QStringLiteral("headerPhraseTimer"));messages->setInterval(6500);
    connect(messages,&QTimer::timeout,this,[this,statusFade](){
        static int phrase=0;
        const QStringList en=QStringList()<<QStringLiteral("Your drivers. Clearly organized.")<<QStringLiteral("Find the right match for your PC.")<<QStringLiteral("A smoother Windows experience.");
        const QStringList ar=QStringList()<<QString::fromUtf8("تعريفات جهازك، بوضوح.")<<QString::fromUtf8("اعثر على المطابقة المناسبة لجهازك.")<<QString::fromUtf8("تجربة ويندوز أكثر سلاسة.");
        phrase=(phrase+1)%en.size();const QString next=m_arabic?ar[phrase]:en[phrase];
        if(qApp->property("reduceMotion").toBool()){statusFade->setOpacity(1);m_status->setText(next);return;}
        QPropertyAnimation *out=new QPropertyAnimation(statusFade,"opacity",m_status);out->setDuration(120);out->setStartValue(1.0);out->setEndValue(0.0);
        connect(out,&QPropertyAnimation::finished,this,[this,statusFade,next](){m_status->setText(next);QPropertyAnimation *in=new QPropertyAnimation(statusFade,"opacity",m_status);in->setDuration(180);in->setStartValue(0.0);in->setEndValue(1.0);in->start(QAbstractAnimation::DeleteWhenStopped);});out->start(QAbstractAnimation::DeleteWhenStopped);
    });messages->start();
    l->addSpacing(12);
    m_theme=createButton(QStringLiteral("moon"),QStringLiteral("titleAction"),QStringLiteral("Switch to dark mode"));m_theme->setObjectName(QStringLiteral("titleThemeToggle"));m_theme->setCheckable(true);l->addWidget(m_theme);
    m_refresh = createButton(QStringLiteral("refresh"),QStringLiteral("titleAction"),QStringLiteral("Refresh devices"));
    m_refresh->hide();
    m_collapse = createButton(QStringLiteral("panel-left"),QStringLiteral("titleAction"),QStringLiteral("Collapse sidebar"));
    m_collapse->setObjectName(QStringLiteral("titleSidebarToggle"));
    l->addWidget(m_collapse);
    m_menu = createButton(QStringLiteral("menu"),QStringLiteral("titleAction"),QStringLiteral("Tools and utilities"));
    l->addWidget(m_menu);
    l->addSpacing(12);
    m_min = createButton(QStringLiteral("minimize"),QStringLiteral("windowAction"),QStringLiteral("Minimize"));
    l->addWidget(m_min);
    m_max = createButton(QStringLiteral("maximize"),QStringLiteral("windowAction"),QStringLiteral("Maximize / Restore"));
    l->addWidget(m_max);
    m_close = createButton(QStringLiteral("close"),QStringLiteral("windowClose"),QStringLiteral("Close"));
    l->addWidget(m_close);

    connect(m_theme,&QToolButton::clicked,this,&TitleBar::themeRequested);
    connect(m_refresh,&QToolButton::clicked,this,&TitleBar::refreshRequested);
    connect(m_menu,&QToolButton::clicked,this,&TitleBar::menuRequested);
    connect(m_collapse,&QToolButton::clicked,this,&TitleBar::sidebarRequested);
    connect(m_min,&QToolButton::clicked,this,&TitleBar::minimizeRequested);
    connect(m_max,&QToolButton::clicked,this,&TitleBar::maximizeRequested);
    connect(m_close,&QToolButton::clicked,this,&TitleBar::closeRequested);
    updateTooltips();
}

QToolButton *TitleBar::createButton(const QString &symbol,const QString &objectName,const QString &description)
{
    QToolButton *b = new TitleButton(this);
    b->setObjectName(objectName);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::StrongFocus);
    b->setToolTip(description);
    const bool windowAction = objectName != QStringLiteral("titleAction");
    b->setFixedSize(windowAction ? 55 : 48,58);
    b->setIcon(VectorIcons::icon(symbol,QColor("#E3F4F6"),20));
    b->setIconSize(QSize(20,20));
    return b;
}

void TitleBar::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);if(m_status)m_status->setVisible(width()>=1050);}

void TitleBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0,0,width(),0);gradient.setColorAt(0,Theme::color("headerStart"));gradient.setColorAt(1,Theme::color("headerEnd"));p.fillRect(rect(),gradient);
    p.setPen(QColor(255,255,255,24));
    p.drawLine(0,height()-1,width(),height()-1);
}

void TitleBar::setStatus(const QString &text) {
    if(text.contains(QStringLiteral("DEMO")))m_status->setText(m_arabic?QString::fromUtf8("معاينة الواجهة"):QStringLiteral("Interface preview"));
    else m_status->setText(m_arabic?QString::fromUtf8("تعريفات جهازك، بوضوح."):QStringLiteral("Your drivers. Clearly organized."));
}
void TitleBar::setRefreshEnabled(bool enabled) { m_refresh->setEnabled(enabled && !m_workflowLocked); }
void TitleBar::setWorkflowLocked(bool locked){
    m_workflowLocked=locked;m_menu->setEnabled(!locked);m_collapse->setEnabled(!locked);m_refresh->setEnabled(!locked);
}
void TitleBar::setMaximized(bool maximized)
{
    m_maximized = maximized;
    m_max->setIcon(VectorIcons::icon(maximized ? QStringLiteral("restore") : QStringLiteral("maximize"),
                                    QColor("#E3F4F6"),20));
    updateTooltips();
}
void TitleBar::setDark(bool dark){m_dark=dark;m_theme->setChecked(dark);m_theme->setIcon(VectorIcons::icon(dark?QStringLiteral("sun"):QStringLiteral("moon"),QColor("#E3F4F6"),20));updateTooltips();}
void TitleBar::setArabic(bool arabic) { m_arabic=arabic;
    findChild<QLabel*>(QStringLiteral("headerTitle"))->setText(arabic?QString::fromUtf8("درايفر باك مودرن"):QStringLiteral("DriverPack Modern"));
    findChild<QLabel*>(QStringLiteral("headerSub"))->setText(arabic?QString::fromUtf8("واجهة طارق صادق"):QStringLiteral("UI by Tarek Sadek"));
    updateTooltips(); }
void TitleBar::updateTooltips()
{
    m_theme->setToolTip(m_dark?(m_arabic?QString::fromUtf8("التبديل إلى الوضع الفاتح"):QStringLiteral("Switch to light mode")):(m_arabic?QString::fromUtf8("التبديل إلى الوضع الداكن"):QStringLiteral("Switch to dark mode")));m_theme->setAccessibleName(m_theme->toolTip());
    m_refresh->setToolTip(m_arabic ? QString::fromUtf8("تحديث قائمة الأجهزة") : QStringLiteral("Refresh devices"));
    m_menu->setToolTip(m_arabic ? QString::fromUtf8("الأدوات والقائمة") : QStringLiteral("Tools and utilities"));
    m_collapse->setToolTip(m_arabic ? QString::fromUtf8("طي أو فتح القائمة الجانبية") : QStringLiteral("Collapse / expand sidebar"));
    m_min->setToolTip(m_arabic ? QString::fromUtf8("تصغير") : QStringLiteral("Minimize"));
    m_max->setToolTip(m_maximized ? (m_arabic?QString::fromUtf8("استعادة"):QStringLiteral("Restore"))
                                : (m_arabic?QString::fromUtf8("تكبير"):QStringLiteral("Maximize")));
    m_close->setToolTip(m_arabic?QString::fromUtf8("إغلاق"):QStringLiteral("Close"));
}

bool TitleBar::isDragArea(const QPoint &point) const
{
    if (!rect().contains(point)) return false;
    QWidget *child = childAt(point);
    return child == 0 || child == m_status || child->inherits("QLabel");
}
void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isDragArea(event->pos())) {
        m_dragging=true;
        m_dragOffset = event->globalPos()-window()->frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton) && !window()->isMaximized()) {
        window()->move(event->globalPos()-m_dragOffset);
        event->accept(); return;
    }
    QWidget::mouseMoveEvent(event);
}
void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging=false;
    QWidget::mouseReleaseEvent(event);
}
void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button()==Qt::LeftButton && isDragArea(event->pos())) {
        emit maximizeRequested(); event->accept(); return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
