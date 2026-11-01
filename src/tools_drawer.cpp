#include "tools_drawer.h"
#include "appearance.h"
#include "vector_icons.h"
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QToolButton>
#include <QFrame>
#include <QFont>
#include <QPainterPath>

ToolsDrawer::ToolsDrawer(QWidget *parent)
    : QWidget(parent), m_panel(new QWidget(this)), m_title(0), m_close(0),
      m_animation(new QPropertyAnimation(this, "openAmount", this)), m_amount(0), m_open(false),
      m_arabic(false), m_motion(true)
{
    setObjectName(QStringLiteral("toolsOverlay"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    m_panel->setObjectName(QStringLiteral("toolsDrawerPanel"));
    m_panel->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *panel = new QVBoxLayout(m_panel);
    panel->setContentsMargins(0, 0, 0, 14);
    panel->setSpacing(0);

    QWidget *top = new QWidget(m_panel);
    top->setObjectName(QStringLiteral("toolsDrawerHeader"));
    QHBoxLayout *topLine = new QHBoxLayout(top);
    topLine->setContentsMargins(24, 0, 14, 0);
    topLine->setSpacing(9);
    QLabel *symbol = new QLabel(top);
    symbol->setPixmap(VectorIcons::pixmap(QStringLiteral("sliders"), QColor("#B7F4ED"), 23));
    symbol->setFixedSize(28, 28);
    topLine->addWidget(symbol);
    m_title = new QLabel(QStringLiteral("Tools & utilities"), top);
    m_title->setObjectName(QStringLiteral("toolsDrawerTitle"));
    topLine->addWidget(m_title, 1);
    m_close = new TitleButton(top);
    m_close->setObjectName(QStringLiteral("toolsDrawerClose"));
    m_close->setIcon(VectorIcons::icon(QStringLiteral("close"), QColor("#E8F8FA"), 18));
    m_close->setIconSize(QSize(18, 18));
    m_close->setFixedSize(48, 48);
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setToolTip(QStringLiteral("Close tools panel (Esc)"));
    topLine->addWidget(m_close);
    top->setFixedHeight(69);
    panel->addWidget(top);
    connect(m_close, &QToolButton::clicked, this, &ToolsDrawer::closeDrawer);

    QScrollArea *scroll = new QScrollArea(m_panel);
    scroll->setObjectName(QStringLiteral("toolsDrawerScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    QWidget *contents = new QWidget(scroll);
    contents->setObjectName(QStringLiteral("toolsDrawerContent"));
    QVBoxLayout *stack = new QVBoxLayout(contents);
    stack->setContentsMargins(0, 9, 0, 22);
    stack->setSpacing(0);

    addHeading(stack, QStringLiteral("DRIVER MANAGEMENT"), QString::fromUtf8("إدارة التعريفات"));
    addRow(stack, QStringLiteral("offline"), QStringLiteral("Offline packages"),
           QString::fromUtf8("حزم التعريفات المحلية"), QStringLiteral("layers"));
    addRow(stack, QStringLiteral("snapshot"), QStringLiteral("Save device report..."),
           QString::fromUtf8("حفظ تقرير الجهاز..."), QStringLiteral("save"));
    addRow(stack, QStringLiteral("extract"), QStringLiteral("Extract drivers..."),
           QString::fromUtf8("استخراج التعريفات..."), QStringLiteral("install"));
    addRow(stack, QStringLiteral("restore"), QStringLiteral("Create Restore Point"),
           QString::fromUtf8("إنشاء نقطة استعادة"), QStringLiteral("restore"));
    addRow(stack, QStringLiteral("updates"), QStringLiteral("Download indexes & packages"),
           QString::fromUtf8("تنزيل الفهارس والحزم"), QStringLiteral("install"));
    addRow(stack, QStringLiteral("backup"), QStringLiteral("Backup installed drivers (DISM)"),
           QString::fromUtf8("نسخ التعريفات المثبتة (DISM)"), QStringLiteral("save"));
    addHeading(stack, QStringLiteral("WINDOWS UTILITIES"), QString::fromUtf8("أدوات ويندوز"));
    addRow(stack, QStringLiteral("uninstall"), QStringLiteral("Software Uninstall"),
           QString::fromUtf8("إزالة البرامج"), QStringLiteral("layers"));
    addRow(stack, QStringLiteral("device-manager"), QStringLiteral("Device Manager"),
           QString::fromUtf8("إدارة الأجهزة"), QStringLiteral("chip"));
    addRow(stack, QStringLiteral("system-properties"), QStringLiteral("System Properties"),
           QString::fromUtf8("خصائص النظام"), QStringLiteral("computer"));
    addRow(stack, QStringLiteral("display-settings"), QStringLiteral("Display Settings"),
           QString::fromUtf8("إعدادات العرض"), QStringLiteral("monitor"));
    addRow(stack, QStringLiteral("power-options"), QStringLiteral("Power Options"),
           QString::fromUtf8("خيارات الطاقة"), QStringLiteral("power"));
    addRow(stack, QStringLiteral("network-connections"), QStringLiteral("Network Connections"),
           QString::fromUtf8("اتصالات الشبكة"), QStringLiteral("network"));
    addRow(stack, QStringLiteral("computer-management"), QStringLiteral("Computer Management"),
           QString::fromUtf8("إدارة الكمبيوتر"), QStringLiteral("settings"));
    addRow(stack, QStringLiteral("control-panel"), QStringLiteral("Control Panel"),
           QString::fromUtf8("لوحة التحكم"), QStringLiteral("sliders"));
    addRow(stack, QStringLiteral("disk-management"), QStringLiteral("Disk Management"),
           QString::fromUtf8("إدارة الأقراص"), QStringLiteral("storage"));
    addRow(stack, QStringLiteral("task-manager"), QStringLiteral("Task Manager"),
           QString::fromUtf8("مدير المهام"), QStringLiteral("activity"));
    addRow(stack, QStringLiteral("command-line"), QStringLiteral("Command Line"),
           QString::fromUtf8("موجه الأوامر"), QStringLiteral("terminal"));
    addHeading(stack, QStringLiteral("APPLICATION"), QString::fromUtf8("التطبيق"));
    addRow(stack, QStringLiteral("settings"), QStringLiteral("Settings"), QString::fromUtf8("الإعدادات"),
           QStringLiteral("settings"));
    addRow(stack, QStringLiteral("license"), QStringLiteral("License Agreement"),
           QString::fromUtf8("اتفاقية الترخيص"), QStringLiteral("shield"));
    addRow(stack, QStringLiteral("automation"), QStringLiteral("Export installation plan"),
           QString::fromUtf8("تصدير خطة التثبيت"), QStringLiteral("save"));
    addRow(stack, QStringLiteral("activity-log"), QStringLiteral("Activity log"), QString::fromUtf8("سجل النشاط"), QStringLiteral("activity"));
    addRow(stack, QStringLiteral("report"), QStringLiteral("Export support report"),
           QString::fromUtf8("تصدير تقرير الدعم"), QStringLiteral("alert"));
    addRow(stack, QStringLiteral("report-bug"), QStringLiteral("Report a bug"),
           QString::fromUtf8("الإبلاغ عن مشكلة"), QStringLiteral("alert"));
    stack->addStretch(1);
    scroll->setWidget(contents);
    panel->addWidget(scroll, 1);

    m_animation->setDuration(260);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this,
            [this]()
            {
                if (!m_open)
                    hide();
            });
    hide();
}
void ToolsDrawer::addHeading(QVBoxLayout *layout, const QString &english, const QString &arabic)
{
    QLabel *heading = new QLabel(english, m_panel);
    heading->setObjectName(QStringLiteral("toolsDrawerGroup"));
    heading->setContentsMargins(26, 17, 18, 8);
    heading->setMinimumHeight(48);
    layout->addWidget(heading);
    Heading h;
    h.label = heading;
    h.english = english;
    h.arabic = arabic;
    m_headings.append(h);
}
void ToolsDrawer::addRow(QVBoxLayout *layout, const QString &key, const QString &english,
                         const QString &arabic, const QString &symbol, bool enabled)
{
    QToolButton *row = new QToolButton(m_panel);
    row->setObjectName(QStringLiteral("toolsDrawerRow"));
    row->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    row->setAutoRaise(false);
    row->setMouseTracking(true);
    row->setAttribute(Qt::WA_Hover);
    row->setStyleSheet(
        QStringLiteral("QToolButton#toolsDrawerRow{background:transparent;color:#ddf3f1;border:1px solid "
                       "transparent;border-radius:12px;text-align:left;padding:0 22px;margin:2px 10px;} "
                       "QToolButton#toolsDrawerRow:hover:enabled{background:#175e6c;color:white;border-color:"
                       "#377b86;} QToolButton#toolsDrawerRow:pressed:enabled{background:#087e88;} "
                       "QToolButton#toolsDrawerRow:disabled{color:#6d99a0;}"));
    row->setText(english);
    row->setToolTip(english);
    row->setIcon(VectorIcons::icon(symbol, enabled ? QColor("#D4F1F0") : QColor("#6D99A0"), 20));
    row->setIconSize(QSize(20, 20));
    row->setEnabled(enabled);
    if (!enabled)
        row->setToolTip(QStringLiteral(
            "Requires DriverPack engine integration. This preview does not modify drivers or Windows."));
    row->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    row->setMinimumHeight(54);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row->setProperty("toolId", key);
    row->setFocusPolicy(Qt::TabFocus);
    layout->addWidget(row);
    Row r;
    r.button = row;
    r.english = english;
    r.arabic = arabic;
    m_rows.append(r);
    connect(row, &QToolButton::clicked, this,
            [this, key]()
            {
                closeDrawer();
                emit actionRequested(key);
            });
}
void ToolsDrawer::setArabic(bool arabic)
{
    m_arabic = arabic;
    m_title->setText(arabic ? QString::fromUtf8("الأدوات والخدمات") : QStringLiteral("Tools & utilities"));
    for (int i = 0; i < m_headings.size(); ++i)
        m_headings[i].label->setText(arabic ? m_headings.at(i).arabic : m_headings.at(i).english);
    for (int i = 0; i < m_rows.size(); ++i)
        m_rows[i].button->setText(arabic ? m_rows.at(i).arabic : m_rows.at(i).english);
    m_close->setToolTip(arabic ? QString::fromUtf8("إغلاق قائمة الأدوات (Esc)")
                               : QStringLiteral("Close tools panel (Esc)"));
    syncPosition();
}
void ToolsDrawer::setActionAvailable(const QString &id, bool available, const QString &reason)
{
    for (const Row &row : m_rows)
        if (row.button->property("toolId").toString() == id)
        {
            row.button->setEnabled(available);
            row.button->setToolTip(available ? row.button->text() : reason);
            row.button->setCursor(available ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
}
void ToolsDrawer::setMotionEnabled(bool enabled)
{
    m_motion = enabled;
    if (!enabled)
    {
        m_animation->stop();
        setOpenAmount(m_open ? 1.0 : 0.0);
        if (!m_open)
            hide();
    }
}
void ToolsDrawer::setOpenAmount(qreal value)
{
    m_amount = qBound(0.0, value, 1.0);
    syncPosition();
    update();
}
void ToolsDrawer::syncPosition()
{
    const int w = qMin(432, qMax(310, width() - 48));
    const bool rtl = layoutDirection() == Qt::RightToLeft;
    const int x = rtl ? -w + qRound((w + 12) * m_amount) : width() - qRound((w + 12) * m_amount);
    m_panel->setGeometry(x, 8, w, qMax(0, height() - 20));
}
void ToolsDrawer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    syncPosition();
}
void ToolsDrawer::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal radius = window()->isMaximized() ? 0 : 20;
    QPainterPath clip;
    clip.moveTo(0, 0);
    clip.lineTo(width(), 0);
    clip.lineTo(width(), height() - radius);
    clip.quadTo(width(), height(), width() - radius, height());
    clip.lineTo(radius, height());
    clip.quadTo(0, height(), 0, height() - radius);
    clip.closeSubpath();
    p.setClipPath(clip);
    p.fillRect(rect(), QColor(0, 13, 27, qRound(135 * m_amount)));
}
void ToolsDrawer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_panel->geometry().contains(event->pos()))
    {
        closeDrawer();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
void ToolsDrawer::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        closeDrawer();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
void ToolsDrawer::openDrawer()
{
    if (m_open)
        return;
    m_open = true;
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    m_animation->stop();
    if (m_motion)
    {
        m_animation->setStartValue(m_amount);
        m_animation->setEndValue(1.0);
        m_animation->start();
    }
    else
        setOpenAmount(1.0);
}
void ToolsDrawer::closeDrawer()
{
    if (!m_open)
        return;
    m_open = false;
    m_animation->stop();
    if (m_motion)
    {
        m_animation->setStartValue(m_amount);
        m_animation->setEndValue(0.0);
        m_animation->start();
    }
    else
    {
        setOpenAmount(0.0);
        hide();
    }
}
void ToolsDrawer::toggle()
{
    if (m_open)
        closeDrawer();
    else
        openDrawer();
}
