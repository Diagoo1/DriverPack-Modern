#include "ui_font.h"
#include "appearance.h"
#include "ui_language.h"
#include "theme.h"
#include "vector_icons.h"
#include "ui_elements.h"
#include <QApplication>
#include <QScrollBar>
#include <QAbstractScrollArea>
#include <QStyleOptionSlider>
#include <QCursor>
#include <QAbstractButton>
#include <QCheckBox>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QShowEvent>
#include <QEasingCurve>
#include <QScrollArea>
#include <QComboBox>
#include <QTabBar>
#include <QStyleOptionButton>
#include <QCursor>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QStyleOptionMenuItem>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QContextMenuEvent>
#include <QClipboard>
#include <QScopedPointer>
#include <QHelpEvent>
#include <QToolTip>
#include <QDesktopWidget>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>

TitleButton::TitleButton(QWidget *parent)
    : QToolButton(parent), m_hover(0), m_animation(this, "hoverAmount", this)
{
    m_animation.setDuration(155);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
}
void TitleButton::animateHover(qreal value)
{
    m_animation.stop();
    if (qApp->property("reduceMotion").toBool())
    {
        setHoverAmount(value);
        return;
    }
    m_animation.setStartValue(m_hover);
    m_animation.setEndValue(value);
    m_animation.start();
}
void TitleButton::enterEvent(QEvent *event)
{
    if (isEnabled())
        animateHover(1);
    QToolButton::enterEvent(event);
}
void TitleButton::leaveEvent(QEvent *event)
{
    animateHover(0);
    QToolButton::leaveEvent(event);
}
void TitleButton::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange && !isEnabled())
    {
        m_animation.stop();
        setHoverAmount(0);
    }
    QToolButton::changeEvent(event);
}
void TitleButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool close = objectName().contains(QStringLiteral("Close"), Qt::CaseInsensitive);
    const QRectF target = QRectF(rect()).adjusted(7, 10, -7, -10);
    QColor color = close ? QColor("#DC5363") : QColor(Qt::white);
    const bool keyboardFocus = hasFocus() && window()->testAttribute(Qt::WA_KeyboardFocusChange);
    color.setAlpha(qRound((close ? 175 : 25) * (isDown() ? 1 : qMax(m_hover, keyboardFocus ? .45 : 0.0))));
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(target, 10, 10);
    const QSize size = iconSize();
    const QRect symbol((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(),
                       size.height());
    icon().paint(&painter, symbol, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
}

static bool darkSurface(QWidget *widget) { return Theme::isDark(widget); }
QPainterPath windowClip(QWidget *widget, qreal radius)
{
    const QPoint origin = widget->mapTo(widget->window(), QPoint());
    QPainterPath path;
    path.addRoundedRect(QRectF(widget->window()->rect()).translated(-origin.x(), -origin.y()),
                        widget->window()->isMaximized() ? 0 : radius,
                        widget->window()->isMaximized() ? 0 : radius);
    return path;
}
RoundedActivityBar::RoundedActivityBar(QWidget *parent)
    : QProgressBar(parent),m_tick(this),m_phase(0),m_visual(0),m_fill(this,"visualProgress",this)
{
    setRange(0,0);setTextVisible(false);setFixedHeight(10);
    m_tick.setInterval(40);m_fill.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&m_tick,&QTimer::timeout,this,[this](){
        if(!isVisible() || minimum()!=0 || maximum()!=0 || qApp->property("reduceMotion").toBool()){m_tick.stop();return;}
        m_phase=(m_phase+1)%80;update();
    });
    connect(this,&QProgressBar::valueChanged,this,[this](int){animateValue();});
}
void RoundedActivityBar::setVisualProgress(qreal value){m_visual=qBound(qreal(0),value,qreal(1));update();}
int RoundedActivityBar::remainingAnimationMs()const{
    return m_fill.state()==QAbstractAnimation::Running?qMax(0,m_fill.duration()-m_fill.currentTime()):0;
}
void RoundedActivityBar::animateValue(){
    m_fill.stop();
    if(minimum()==0 && maximum()==0){m_visual=0;update();return;}
    m_tick.stop();
    const qreal target=qBound(qreal(0),(qreal(value())-minimum())/qMax(qreal(1),qreal(maximum())-minimum()),qreal(1));
    if(!isVisible() || target<=m_visual || qApp->property("reduceMotion").toBool()) {setVisualProgress(target);return;}
    // The model value and valueChanged signal stay exact. Only the painted fill
    // interpolates; no worker, byte count or installation stage is delayed.
    m_fill.setDuration(qBound(360,360+qRound((target-m_visual)*440),800));
    m_fill.setStartValue(m_visual);m_fill.setEndValue(target);m_fill.start();
}
void RoundedActivityBar::showEvent(QShowEvent *event){
    QProgressBar::showEvent(event);
    m_fill.stop();
    if(minimum()==0 && maximum()==0){m_visual=0;if(!qApp->property("reduceMotion").toBool())m_tick.start();}
    else setVisualProgress(qBound(qreal(0),(qreal(value())-minimum())/qMax(qreal(1),qreal(maximum())-minimum()),qreal(1)));
}
void RoundedActivityBar::hideEvent(QHideEvent *event){m_tick.stop();m_fill.stop();QProgressBar::hideEvent(event);}
void RoundedActivityBar::paintEvent(QPaintEvent *){
    const bool busy=minimum()==0 && maximum()==0;
    if(busy){m_fill.stop();m_visual=0;if(isVisible() && !m_tick.isActive() && !qApp->property("reduceMotion").toBool())m_tick.start();}
    else m_tick.stop();
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
    const bool dark=darkSurface(this),hero=property("heroProgress").toBool();
    const QRectF face=QRectF(rect()).adjusted(.5,.5,-.5,-.5);const qreal radius=face.height()/2;
    QPainterPath clip;clip.addRoundedRect(face,radius,radius);
    painter.fillPath(clip,hero?QColor(255,255,255,35):Theme::color("elevated",dark));painter.setClipPath(clip);
    QRectF fill=face;
    if(busy){fill.setWidth(face.width()*.30);fill.moveLeft(face.left()+(face.width()+fill.width())*(qApp->property("reduceMotion").toBool()?.5:qreal(m_phase)/79)-fill.width());}
    else fill.setWidth(face.width()*m_visual);
    if((layoutDirection()==Qt::RightToLeft)!=invertedAppearance())fill.moveLeft(face.right()-(fill.right()-face.left()));
    QLinearGradient gradient(fill.topLeft(),fill.topRight());
    gradient.setColorAt(0,hero?QColor("#31bfd1"):Theme::color("headerStart",dark));
    gradient.setColorAt(1,hero?QColor("#4bddb6"):Theme::color("accent",dark));
    painter.setPen(Qt::NoPen);painter.setBrush(gradient);painter.drawRoundedRect(fill,radius,radius);
    if(!busy && isTextVisible()){painter.setPen(hero?QColor(Qt::white):Theme::color("text",dark));painter.drawText(rect(),Qt::AlignCenter,text());}
}

ActionButton::ActionButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent), m_hover(0), m_animation(this, "hoverAmount", this)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(38);
    m_animation.setDuration(155);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
}
QSize ActionButton::sizeHint() const
{
    const QSize native = QPushButton::sizeHint();
    return QSize(qMax(native.width(),
                      fontMetrics().width(text()) + 32 + (icon().isNull() ? 0 : iconSize().width() + 9)),
                 qMax(38, native.height()));
}
void ActionButton::animate(qreal value)
{
    m_animation.stop();
    if (qApp->property("reduceMotion").toBool())
    {
        setHoverAmount(value);
        return;
    }
    m_animation.setStartValue(m_hover);
    m_animation.setEndValue(value);
    m_animation.start();
}
void ActionButton::enterEvent(QEvent *event)
{
    if (isEnabled())
        animate(1);
    QPushButton::enterEvent(event);
}
void ActionButton::leaveEvent(QEvent *event)
{
    animate(0);
    QPushButton::leaveEvent(event);
}
static void paintActionSurface(QAbstractButton *button, qreal hoverAmount)
{
    QPainter p(button);
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = darkSurface(button);
    const bool command = button->property("workspaceCommand").toBool();
    const bool primary =
        button->objectName() == QStringLiteral("primaryButton") || button->property("primaryAction").toBool();
    const bool quiet = button->objectName().contains(QStringLiteral("Link"), Qt::CaseInsensitive);
    const qreal amount = button->isEnabled() ? (button->isDown() ? 1 : hoverAmount) : 0;
    QColor base =
        primary ? Theme::color("accent", dark) : Theme::color(command ? "elevated" : "surface", dark);
    QColor hover = primary ? Theme::color("accentHover", dark) : Theme::color("elevated", dark);
    auto mix = [amount](int a, int b) { return qRound(a + (b - a) * amount); };
    QColor fill(mix(base.red(), hover.red()), mix(base.green(), hover.green()),
                mix(base.blue(), hover.blue()));
    if (quiet && !command)
    {
        const QColor resting = Theme::color("elevated", dark);
        fill = QColor(mix(resting.red(), hover.red()), mix(resting.green(), hover.green()),
                      mix(resting.blue(), hover.blue()));
    }
    if (command)
    {
        const QColor rest = Theme::color("elevated", dark), ink = Theme::color("link", dark);
        const qreal tint = amount * .10;
        fill = QColor(qRound(rest.red() * (1 - tint) + ink.red() * tint),
                      qRound(rest.green() * (1 - tint) + ink.green() * tint),
                      qRound(rest.blue() * (1 - tint) + ink.blue() * tint));
    }
    if (!button->isEnabled())
        p.setOpacity(.45);
    p.setPen(primary ? Qt::NoPen
                     : QPen(command ? Theme::color("border", dark)
                                    : (dark ? QColor("#426474") : QColor("#D5E5E9")),
                            1));
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(button->rect()).adjusted(.5, .5, -.5, -.5), 11, 11);
    p.setPen(primary
                 ? QColor("#FFFFFF")
                 : (command ? Theme::color("text", dark) : (dark ? QColor("#E6F7FA") : QColor("#226071"))));
    QFont face = button->font();
    face.setFamily(UiFont::family());
    face.setStyleHint(QFont::SansSerif);
    face.setStyleStrategy(QFont::PreferAntialias);
    if (command)
        face.setWeight(QFont::Medium);
    p.setFont(face);
    QRect area = button->rect().adjusted(14, 0, -14, 0);
    if (!button->icon().isNull())
    {
        const int total = p.fontMetrics().width(button->text()) + button->iconSize().width() + 9;
        const bool rtl = button->layoutDirection() == Qt::RightToLeft;
        const int x =
            rtl ? (button->width() + total) / 2 - button->iconSize().width() : (button->width() - total) / 2;
        button->icon().paint(&p, QRect(x, (button->height() - button->iconSize().height()) / 2,
                                       button->iconSize().width(), button->iconSize().height()));
        if (rtl)
            area.setRight(x - 9);
        else
            area.setLeft(x + button->iconSize().width() + 9);
        p.drawText(area, Qt::AlignAbsolute | (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                   button->text());
    }
    else
        p.drawText(area, Qt::AlignCenter, button->text());
    if (button->hasFocus() && button->window()->testAttribute(Qt::WA_KeyboardFocusChange))
    {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#20BCAF"), 1.3));
        p.drawRoundedRect(QRectF(button->rect()).adjusted(3, 3, -3, -3), 8, 8);
    }
}
void ActionButton::paintEvent(QPaintEvent *) { paintActionSurface(this, m_hover); }
CommandToolButton::CommandToolButton(QWidget *parent)
    : QToolButton(parent), m_hover(0), m_animation(this, "hoverAmount", this)
{
    setProperty("workspaceCommand", true);
    setCursor(Qt::PointingHandCursor);
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setIconSize(QSize(18, 18));
    setFixedHeight(40);
    m_animation.setDuration(155);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
}
QSize CommandToolButton::sizeHint() const
{
    return QSize(fontMetrics().width(text()) + 32 + (icon().isNull() ? 0 : iconSize().width() + 9), 40);
}
void CommandToolButton::animate(qreal value)
{
    m_animation.stop();
    if (qApp->property("reduceMotion").toBool())
    {
        setHoverAmount(value);
        return;
    }
    m_animation.setStartValue(m_hover);
    m_animation.setEndValue(value);
    m_animation.start();
}
void CommandToolButton::enterEvent(QEvent *event)
{
    if (isEnabled())
        animate(1);
    QToolButton::enterEvent(event);
}
void CommandToolButton::leaveEvent(QEvent *event)
{
    animate(0);
    QToolButton::leaveEvent(event);
}
void CommandToolButton::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange && !isEnabled())
    {
        m_animation.stop();
        setHoverAmount(0);
    }
    QToolButton::changeEvent(event);
}
void CommandToolButton::paintEvent(QPaintEvent *) { paintActionSurface(this, m_hover); }
DialogHeader::DialogHeader(const QString &title, const QString &symbol, QDialog *dialog, bool arabic)
    : QWidget(dialog), m_dragging(false)
{
    setFixedHeight(64);
    setObjectName(QStringLiteral("dialogHeader"));
    QHBoxLayout *line = new QHBoxLayout(this);
    line->setContentsMargins(22, 0, 9, 0);
    line->setSpacing(12);
    QLabel *icon = new QLabel(this);
    icon->setPixmap(VectorIcons::pixmap(symbol, QColor("#BDF7F1"), 25));
    line->addWidget(icon);
    QLabel *name = new QLabel(title, this);
    name->setAlignment(Qt::AlignAbsolute | (arabic ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    name->setStyleSheet(
        QStringLiteral("background:transparent;color:#FFFFFF;font-size:17px;font-weight:600;"));
    line->addWidget(name, 1);
    TitleButton *close = new TitleButton(this);
    close->setObjectName(QStringLiteral("windowClose"));
    close->setIcon(VectorIcons::icon(QStringLiteral("close"), QColor("#FFFFFF"), 19));
    close->setIconSize(QSize(19, 19));
    close->setFixedSize(48, 54);
    close->setCursor(Qt::PointingHandCursor);
    close->setToolTip(arabic ? QString::fromUtf8("إغلاق") : QStringLiteral("Close"));
    line->addWidget(close);
    connect(close, &QToolButton::clicked, dialog, &QDialog::reject);
}
void DialogHeader::paintEvent(QPaintEvent *) { /* Painted once by RoundedDialog to preserve outer alpha. */ }
void DialogHeader::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_offset = e->globalPos() - window()->pos();
        e->accept();
    }
    else
        QWidget::mousePressEvent(e);
}
void DialogHeader::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging && (e->buttons() & Qt::LeftButton))
    {
        window()->move(e->globalPos() - m_offset);
        e->accept();
    }
    else
        QWidget::mouseMoveEvent(e);
}
void DialogHeader::mouseReleaseEvent(QMouseEvent *e)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(e);
}
void AccentCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, width(), height());
    g.setColorAt(0, Theme::color("heroStart"));
    g.setColorAt(.65, Theme::color("heroMiddle"));
    g.setColorAt(1, Theme::color("heroEnd"));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(rect()), 20, 20);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 20, 20);
    p.setClipPath(clip);
    p.setBrush(QColor(255, 255, 255, 9));
    p.drawEllipse(QPointF(width() - 55, 20), 125, 125);
    p.drawEllipse(QPointF(width() - 50, 20), 80, 80);
}
void SurfaceCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = darkSurface(this);
    const QRectF face = QRectF(rect()).adjusted(1, 1, -1, -4);
    p.setPen(Qt::NoPen);
    for (int i = 3; i >= 1; --i)
    {
        p.setBrush(QColor(0, 0, 0, dark ? 9 : 5));
        p.drawRoundedRect(face.translated(0, i).adjusted(-.3 * i, 0, .3 * i, .2 * i), 16, 16);
    }
    p.setPen(QPen(Theme::color("border", dark), 1));
    p.setBrush(Theme::color("surface", dark));
    p.drawRoundedRect(face, 16, 16);
    QColor tint = property("cardTint").value<QColor>();
    if (tint.isValid())
    {
        tint.setAlpha(dark ? 30 : 18);
        QLinearGradient wash(face.topLeft(), face.bottomRight());
        wash.setColorAt(0, tint);
        tint.setAlpha(0);
        wash.setColorAt(1, tint);
        p.setPen(Qt::NoPen);
        p.setBrush(wash);
        p.drawRoundedRect(face, 16, 16);
    }
    const QString tone = property("tone").toString();
    if (!tone.isEmpty())
    {
        QColor color =
            tone == QStringLiteral("blue") || tone == QStringLiteral("system")       ? QColor("#309AC6")
            : tone == QStringLiteral("violet") || tone == QStringLiteral("firmware") ? QColor("#9B80CB")
            : tone == QStringLiteral("amber") || tone == QStringLiteral("hardware")  ? QColor("#CC9743")
                                                                                     : QColor("#14AA9D");
        QLinearGradient edge(face.left() + 18, 0, face.right() - 18, 0);
        QColor transparent = color;
        transparent.setAlpha(0);
        edge.setColorAt(0, transparent);
        edge.setColorAt(.22, color);
        edge.setColorAt(.78, color);
        edge.setColorAt(1, transparent);
        p.setPen(QPen(QBrush(edge), 2.3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(face.left() + 18, face.bottom() - .6),
                   QPointF(face.right() - 18, face.bottom() - .6));
    }
}
ProjectMenu::ProjectMenu(QWidget *parent) : QMenu(parent), m_highlightY(-1), m_slide(this, "highlightY", this)
{
    setMinimumWidth(240);
    setStyleSheet(QStringLiteral("QMenu{background:transparent;border:0;padding:14px;font-size:13px;font-"
                                 "weight:400;} QMenu::item{background:transparent;padding:11px 22px;} "
                                 "QMenu::separator{height:1px;background:%1;margin:5px 10px;}")
                      .arg(darkSurface(this) ? QStringLiteral("#3B5868") : QStringLiteral("#E2EBEF")));
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    m_slide.setDuration(120);
    m_slide.setEasingCurve(QEasingCurve::OutCubic);
    connect(this, &QMenu::hovered, this,
            [this](QAction *action)
            {
                if (!action || !action->isEnabled() || action->isSeparator())
                    return;
                const qreal y = actionGeometry(action).top();
                m_slide.stop();
                if (m_highlightY < 0 || qApp->property("reduceMotion").toBool())
                {
                    setHighlightY(y);
                    return;
                }
                m_slide.setStartValue(m_highlightY);
                m_slide.setEndValue(y);
                m_slide.start();
            });
}
void ProjectMenu::showEvent(QShowEvent *event)
{
    QMenu::showEvent(event);
    if (!parentWidget())
        return;
    const QRect bounds = QApplication::desktop()->availableGeometry(this).intersected(
        parentWidget()->window()->frameGeometry().adjusted(10, 10, -10, -10));
    if (bounds.isEmpty())
        return;
    move(qBound(bounds.left(), x(), qMax(bounds.left(), bounds.right() - width() + 1)),
         qBound(bounds.top(), y(), qMax(bounds.top(), bounds.bottom() - height() + 1)));
}
void ProjectMenu::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = darkSurface(this);
    const QRectF face = QRectF(rect()).adjusted(6.5, 6.5, -6.5, -8.5);
    paintPopupShadow(&p, face, 14, dark);
    p.setPen(QPen(Theme::color("border", dark), 1));
    p.setBrush(Theme::color("surface", dark));
    p.drawRoundedRect(face, 14, 14);
    QAction *active = activeAction();
    if (active && active->isEnabled() && !active->isSeparator())
    {
        QRectF target = actionGeometry(active).adjusted(4, 1, -4, -1);
        if (m_highlightY >= 0)
            target.moveTop(m_highlightY + 1);
        p.setPen(Qt::NoPen);
        p.setBrush(dark ? QColor("#2B5264") : QColor("#E4F3EF"));
        p.drawRoundedRect(target, 8, 8);
    }
    for (QAction *action : actions())
    {
        if (!action->isVisible())
            continue;
        const QRect area = actionGeometry(action);
        if (action->isSeparator())
        {
            p.setPen(dark ? QColor("#3B5868") : QColor("#E2EBEF"));
            p.drawLine(area.left() + 10, area.center().y(), area.right() - 10, area.center().y());
            continue;
        }
        QFont face = font();
        face.setFamily(UiFont::family());
        face.setStyleHint(QFont::SansSerif);
        face.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(face);
        p.setPen(action->isEnabled() ? (dark ? QColor("#E7F3F7") : QColor("#234A58"))
                                     : (dark ? QColor("#91ACBA") : QColor("#8196A3")));
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        QRect label = area.adjusted(22, 0, -22, 0);
        const QString text = action->text().section(QLatin1Char('\t'), 0, 0).remove(QLatin1Char('&'));
        const QString shortcut = action->text().contains(QLatin1Char('\t'))
                                     ? action->text().section(QLatin1Char('\t'), 1)
                                     : action->shortcut().toString(QKeySequence::NativeText);
        if (!shortcut.isEmpty())
        {
            const int reserve = p.fontMetrics().width(shortcut) + 24;
            QRect key = label;
            if (rtl)
            {
                label.adjust(reserve, 0, 0, 0);
                key.setRight(label.left() - 12);
            }
            else
            {
                label.adjust(0, 0, -reserve, 0);
                key.setLeft(label.right() + 12);
            }
            p.save();
            p.setPen(Theme::color("muted", dark));
            p.drawText(key, Qt::AlignAbsolute | (rtl ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
                       shortcut);
            p.restore();
        }
        p.drawText(label, Qt::AlignAbsolute | (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                   text);
        if (action->isCheckable() && action->isChecked())
            VectorIcons::paint(
                &p, QStringLiteral("check"),
                QRectF(rtl ? area.right() - 19 : area.left() + 3, area.center().y() - 7, 14, 14),
                QColor("#00A99E"));
    }
}
// A few translucent rounded layers in a reserved gutter; no live blur or graphics effect.
void paintPopupShadow(QPainter *painter, const QRectF &face, qreal radius, bool dark)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    for (int spread = 6; spread >= 1; --spread)
    {
        painter->setBrush(QColor(0, 0, 0, dark ? 9 : 5));
        painter->drawRoundedRect(face.translated(0, 1).adjusted(-spread, -spread, spread, spread),
                                 radius + spread, radius + spread);
    }
    painter->restore();
}
RoundedPanel::RoundedPanel(QWidget *p) : QWidget(p)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setContentsMargins(6, 6, 6, 8);
}
void RoundedPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF face = QRectF(contentsRect()).adjusted(.5, .5, -.5, -.5);
    paintPopupShadow(&p, face, 16, darkSurface(this));
    p.setPen(QPen(Theme::color("border", darkSurface(this)), 1));
    p.setBrush(Theme::color("surface", darkSurface(this)));
    p.drawRoundedRect(face, 16, 16);
}
RoundedDialog::RoundedDialog(QWidget *p) : QDialog(p, Qt::Dialog | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setContentsMargins(6, 6, 6, 8);
    setModal(true);
}
void RoundedDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF face = QRectF(contentsRect());
    paintPopupShadow(&p, face, 22, darkSurface(this));
    QPainterPath frame;
    frame.addRoundedRect(face, 22, 22);
    p.setPen(Qt::NoPen);
    QWidget *header = findChild<QWidget *>(QStringLiteral("dialogHeader"));
    if (header)
    {
        const QRect bar = header->geometry();
        p.save();
        p.setClipRect(QRect(0, bar.bottom() + 1, width(), height() - bar.height()));
        p.setBrush(Theme::color("background", darkSurface(this)));
        p.drawPath(frame);
        p.restore();
        QLinearGradient g(0, 0, width(), 0);
        g.setColorAt(0, Theme::color("headerStart"));
        g.setColorAt(1, Theme::color("headerEnd"));
        p.save();
        p.setClipRect(bar);
        p.setBrush(g);
        p.drawPath(frame);
        p.restore();
    }
    else
    {
        p.setBrush(Theme::color("background", darkSurface(this)));
        p.drawPath(frame);
    }
}
void RoundedDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    if (qApp->property("reduceMotion").toBool())
        return;
    QPropertyAnimation *fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(180);
    fade->setStartValue(.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}
HoverSurface::HoverSurface(QWidget *p) : QWidget(p), m_amount(0), m_animation(this, "amount", this)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setObjectName(QStringLiteral("motionHighlight"));
    m_animation.setDuration(180);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
    setGeometry(p->rect());
    show();
}
void HoverSurface::transition(bool entered)
{
    m_animation.stop();
    if (qApp->property("reduceMotion").toBool())
    {
        setAmount(entered ? 1 : 0);
        return;
    }
    m_animation.setStartValue(m_amount);
    m_animation.setEndValue(entered ? 1.0 : 0.0);
    m_animation.start();
}
void HoverSurface::paintEvent(QPaintEvent *)
{
    if (m_amount < .001)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool close = parentWidget()->objectName().contains(QStringLiteral("Close"), Qt::CaseInsensitive);
    const QColor tint = close ? QColor("#E24F65") : QColor("#00B7AB");
    QColor fill = tint;
    fill.setAlpha(qRound((close ? 75 : 18) * m_amount));
    QColor border = tint;
    border.setAlpha(qRound(28 * m_amount));
    p.setBrush(fill);
    p.setPen(QPen(border, 1));
    QRectF area = QRectF(rect()).adjusted(1, 1, -1, -1);
    if (QCheckBox *check = qobject_cast<QCheckBox *>(parentWidget()))
    {
        QStyleOptionButton option;
        option.initFrom(check);
        option.text = check->text();
        area = check->style()
                   ->subElementRect(QStyle::SE_CheckBoxIndicator, &option, check)
                   .adjusted(-2, -2, 2, 2);
    }
    p.drawRoundedRect(area, 5, 5);
}
static bool textContextMenu(QWidget *widget, QContextMenuEvent *event)
{
    if (!widget || widget->contextMenuPolicy() != Qt::DefaultContextMenu)
        return false;
    if (widget->parentWidget() && (qobject_cast<QTextEdit *>(widget->parentWidget()) ||
                                   qobject_cast<QPlainTextEdit *>(widget->parentWidget())))
        widget = widget->parentWidget();
    QScopedPointer<QMenu> standard;
    QLabel *label = qobject_cast<QLabel *>(widget);
    if (QLineEdit *edit = qobject_cast<QLineEdit *>(widget))
        standard.reset(edit->createStandardContextMenu());
    else if (QTextEdit *edit = qobject_cast<QTextEdit *>(widget))
        standard.reset(edit->createStandardContextMenu());
    else if (QPlainTextEdit *edit = qobject_cast<QPlainTextEdit *>(widget))
        standard.reset(edit->createStandardContextMenu());
    else if (!label ||
             !(label->textInteractionFlags() & (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard)))
        return false;
    ProjectMenu menu(widget);
    menu.setObjectName(QStringLiteral("textContextMenu"));
    menu.setLayoutDirection(UiLanguage::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);
    if (standard)
    {
        for (QAction *action : standard->actions())
        {
            const QString original = action->text(),
                          name = original.section(QLatin1Char('\t'), 0, 0).remove(QLatin1Char('&'));
            const QStringList keys = QStringList() << QStringLiteral("Undo") << QStringLiteral("Redo")
                                                   << QStringLiteral("Cut") << QStringLiteral("Copy")
                                                   << QStringLiteral("Paste") << QStringLiteral("Delete")
                                                   << QStringLiteral("Select all");
            for (const QString &key : keys)
                if (name.compare(key, Qt::CaseInsensitive) == 0)
                {
                    action->setText(UiLanguage::text(key) +
                                    (original.contains(QLatin1Char('\t'))
                                         ? QLatin1Char('\t') + original.section(QLatin1Char('\t'), 1)
                                         : QString()));
                    break;
                }
            menu.addAction(action);
        }
    }
    else
    {
        QAction *copy = menu.addAction(UiLanguage::text(QStringLiteral("Copy"), QString::fromUtf8("نسخ")));
        copy->setShortcut(QKeySequence::Copy);
        copy->setEnabled(!label->text().isEmpty());
        QObject::connect(copy, &QAction::triggered, label,
                         [label]()
                         {
                             QString value = label->hasSelectedText() ? label->selectedText() : label->text();
                             value.remove(QChar(0x202a));
                             value.remove(QChar(0x202c));
                             qApp->clipboard()->setText(value);
                         });
        menu.addSeparator();
        QAction *all =
            menu.addAction(UiLanguage::text(QStringLiteral("Select all"), QString::fromUtf8("تحديد الكل")));
        all->setShortcut(QKeySequence::SelectAll);
        all->setEnabled(!label->text().isEmpty());
        QObject::connect(all, &QAction::triggered, label,
                         [label]() { label->setSelection(0, label->text().size()); });
    }
    menu.exec(event->globalPos());
    event->accept();
    return true;
}
class RoundedScrollBar : public QScrollBar
{
  public:
    RoundedScrollBar(Qt::Orientation orientation, QWidget *parent) : QScrollBar(orientation, parent)
    {
        setProperty("paintedScroll", true);
        setMouseTracking(true);
    }

  protected:
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        if (maximum() <= minimum())
            return;
        const QRect thumb =
            style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, this);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(thumb.contains(mapFromGlobal(QCursor::pos()))
                             ? Theme::color("link", darkSurface(this))
                             : Theme::color("scrollbar", darkSurface(this)));
        if (window()->property("readOnlyPreview").toBool() && property("paintedThumb").toRect() != thumb)
            setProperty("paintedThumb", thumb);
        const QRectF face = QRectF(thumb).adjusted(2, 2, -2, -2);
        painter.drawRoundedRect(face, qMin(face.width(), face.height()) / 2,
                                qMin(face.width(), face.height()) / 2);
    }
};
class ProjectTip : public RoundedPanel
{
  public:
    explicit ProjectTip(QWidget *owner, const QString &text, const QPoint &position) : RoundedPanel(owner)
    {
        setObjectName(QStringLiteral("projectTooltip"));
        setProperty("tipText", text);
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setProperty("darkMode", Theme::isDark(owner));
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 14, 18, 16);
        auto label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setMaximumWidth(310);
        label->setStyleSheet(
            QStringLiteral("color:%1;font-size:12px;background:transparent;border:0;padding:0;")
                .arg(Theme::color("text", Theme::isDark(owner)).name()));
        label->ensurePolished();
        label->setMinimumWidth(qMin(310, label->fontMetrics().width(text) + 4));
        layout->addWidget(label);
        adjustSize();
        QRect screen = QApplication::desktop()->availableGeometry(owner).intersected(
            owner->window()->frameGeometry().adjusted(10, 10, -10, -10));
        if (screen.isEmpty())
            screen = QApplication::desktop()->availableGeometry(owner);
        setMaximumSize(screen.size());
        QPoint point = position + QPoint(14, 20);
        point.setX(qBound(screen.left() + 4, point.x(), screen.right() - width() - 4));
        if (point.y() + height() > screen.bottom() - 4)
            point.setY(position.y() - height() - 8);
        point.setY(
            qBound(screen.top() + 4, point.y(), qMax(screen.top() + 4, screen.bottom() - height() - 4)));
        move(point);
        QTimer::singleShot(7000, this, &QWidget::close);
    }
};
class InterfaceMotion : public QObject
{
    QPointer<ProjectTip> tip;
    QElapsedTimer lastClick;

  public:
    explicit InterfaceMotion(QObject *p) : QObject(p) {}
    bool eventFilter(QObject *object, QEvent *event) Q_DECL_OVERRIDE
    {
        if (event->type() == QEvent::ToolTip)
        {
            if (QWidget *owner = qobject_cast<QWidget *>(object))
            {
                if (!owner->toolTip().isEmpty())
                {
                    if (lastClick.isValid() && lastClick.elapsed() < 900)
                    {
                        event->accept();
                        return true;
                    }
                    if (tip && tip->isVisible() && tip->parentWidget() == owner &&
                        tip->property("tipText").toString() == owner->toolTip())
                    {
                        event->accept();
                        return true;
                    }
                    if (tip)
                        delete tip.data();
                    auto help = static_cast<QHelpEvent *>(event);
                    QToolTip::hideText();
                    tip = new ProjectTip(owner, owner->toolTip(), help->globalPos());
                    tip->show();
                    event->accept();
                    return true;
                }
            }
        }
        if (event->type() == QEvent::MouseButtonPress)
            lastClick.restart();
        if (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::Wheel)
        {
            if (tip)
                tip->hide();
        }
        if (event->type() == QEvent::Polish)
        {
            if (QLabel *tip = qobject_cast<QLabel *>(object))
                if (tip->windowType() == Qt::ToolTip)
                {
                    tip->setMaximumWidth(360);
                    tip->setWordWrap(true);
                    tip->setStyleSheet(
                        QStringLiteral("QLabel{font-size:12px;padding:7px 10px;border-radius:8px;}"));
                }
        }
        if (event->type() == QEvent::Polish)
        {
            if (auto area = qobject_cast<QAbstractScrollArea *>(object))
            {
                if (!area->verticalScrollBar()->property("paintedScroll").toBool())
                    area->setVerticalScrollBar(new RoundedScrollBar(Qt::Vertical, area));
                if (!area->horizontalScrollBar()->property("paintedScroll").toBool())
                    area->setHorizontalScrollBar(new RoundedScrollBar(Qt::Horizontal, area));
            }
        }
        if (QScrollBar *scroll = qobject_cast<QScrollBar *>(object))
        {
            if (scroll->property("paintedScroll").toBool())
                return false;
            if (event->type() == QEvent::Polish)
            {
                scroll->setAttribute(Qt::WA_OpaquePaintEvent, false);
                scroll->setAutoFillBackground(false);
            }
            if (event->type() == QEvent::Paint)
            {
                if (scroll->maximum() > scroll->minimum())
                {
                    QStyleOptionSlider option;
                    option.initFrom(scroll);
                    option.orientation = scroll->orientation();
                    option.minimum = scroll->minimum();
                    option.maximum = scroll->maximum();
                    option.sliderPosition = scroll->sliderPosition();
                    option.sliderValue = scroll->value();
                    option.pageStep = scroll->pageStep();
                    option.singleStep = scroll->singleStep();
                    option.upsideDown =
                        scroll->invertedAppearance() ^ (scroll->orientation() == Qt::Horizontal &&
                                                        scroll->layoutDirection() == Qt::RightToLeft);
                    option.direction = Qt::LeftToRight;
                    if (scroll->orientation() == Qt::Horizontal)
                        option.state |= QStyle::State_Horizontal;
                    const QRect thumb = scroll->style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                                                        QStyle::SC_ScrollBarSlider, scroll);
                    QPainter painter(scroll);
                    painter.setRenderHint(QPainter::Antialiasing);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(thumb.contains(scroll->mapFromGlobal(QCursor::pos()))
                                         ? Theme::color("link", darkSurface(scroll))
                                         : Theme::color("scrollbar", darkSurface(scroll)));
                    const QRectF shape = QRectF(thumb).adjusted(1, 1, -1, -1);
                    painter.drawRoundedRect(shape, qMin(shape.width(), shape.height()) / 2,
                                            qMin(shape.width(), shape.height()) / 2);
                }
                return true;
            }
        }
        if (event->type() == QEvent::ContextMenu)
        {
            QWidget *widget = qobject_cast<QWidget *>(object);
            if (widget && textContextMenu(widget, static_cast<QContextMenuEvent *>(event)))
                return true;
        }
        if (QAbstractButton *action = qobject_cast<QAbstractButton *>(object))
            if (event->type() == QEvent::Polish || event->type() == QEvent::EnabledChange)
                action->setCursor(action->isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (event->type() == QEvent::Polish || event->type() == QEvent::EnabledChange)
        {
            QWidget *interactive = qobject_cast<QComboBox *>(object);
            if (!interactive)
                interactive = qobject_cast<QTabBar *>(object);
            if (interactive)
                interactive->setCursor(interactive->isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
        return false;
    }
};
void installInterfaceMotion() { qApp->installEventFilter(new InterfaceMotion(qApp)); }
bool projectMessage(QWidget *parent, const QString &title, const QString &message, bool question, bool arabic)
{
    arabic = arabic || UiLanguage::isRtl();
    RoundedDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("projectDialog"));
    dialog.setWindowTitle(title);
    dialog.setLayoutDirection(arabic ? Qt::RightToLeft : Qt::LeftToRight);
    dialog.setStyleSheet(Theme::styleSheet(darkSurface(parent)));
    dialog.setMinimumWidth(480);
    dialog.setMaximumWidth(560);
    QVBoxLayout *frame = new QVBoxLayout(&dialog);
    frame->setContentsMargins(0, 0, 0, 0);
    frame->setSpacing(0);
    frame->addWidget(new DialogHeader(title, question ? QStringLiteral("shield") : QStringLiteral("info"),
                                      &dialog, arabic));
    QWidget *content = new QWidget(&dialog);
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 23, 26, 23);
    layout->setSpacing(18);
    QLabel *brand = new QLabel(arabic ? QString::fromUtf8("درايفر باك مودرن · واجهة طارق صادق")
                                      : QStringLiteral("DriverPack Modern · UI by Tarek Sadek"),
                               content);
    brand->setObjectName(QStringLiteral("detailHeading"));
    brand->setProperty("skipTranslation", true);
    brand->setAlignment(Qt::AlignAbsolute | (arabic ? Qt::AlignRight : Qt::AlignLeft));
    layout->addWidget(brand);
    QLabel *body = new QLabel(message, content);
    body->setObjectName(QStringLiteral("bodyText"));
    body->setWordWrap(true);
    body->setTextFormat(Qt::PlainText);
    body->setAlignment(Qt::AlignAbsolute | (arabic ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop);
    body->setMinimumWidth(420);
    if (message.size() > 500 || message.count(QLatin1Char('\n')) > 6)
    {
        QScrollArea *scroll = new QScrollArea(content);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(body);
        scroll->setMinimumHeight(180);
        scroll->setMaximumHeight(320);
        layout->addWidget(scroll);
    }
    else
        layout->addWidget(body);
    QHBoxLayout *actions = new QHBoxLayout;
    actions->setSpacing(10);
    actions->addStretch();
    if (question)
    {
        QPushButton *cancel =
            new ActionButton(arabic ? QString::fromUtf8("إلغاء") : QStringLiteral("Cancel"), content);
        cancel->setObjectName(QStringLiteral("outlineButton"));
        cancel->setDefault(true);
        cancel->setFocus();
        actions->addWidget(cancel);
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    }
    QPushButton *accept =
        new ActionButton(arabic ? (question ? QString::fromUtf8("متابعة") : QString::fromUtf8("حسنًا"))
                                : (question ? QStringLiteral("Continue") : QStringLiteral("OK")),
                         content);
    accept->setObjectName(QStringLiteral("primaryButton"));
    accept->setDefault(!question);
    actions->addWidget(accept);
    QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addLayout(actions);
    frame->addWidget(content);
    return dialog.exec() == QDialog::Accepted;
}
