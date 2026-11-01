// SPDX-License-Identifier: GPL-3.0-or-later
// Independent native screen implementation. The DriverPack distribution is the
// screen/workflow reference; no original JavaScript or HTA is executed here.
#include "driverpack_shell.h"
#include "activity_log.h"
#include "about_page.h"
#include "appearance.h"
#include "app_settings.h"
#include "engine_bridge.h"
#include "hardware_report.h"
#include "online_worker.h"
#include "portable_library.h"
#include "support_report.h"
#include "system_profile.h"
#include "theme.h"
#include "tools_drawer.h"
#include "title_bar.h"
#include "sidebar.h"
#include "searchable_language_combo.h"
#include "ui_elements.h"
#include "automation_plan.h"
#include "ui_font.h"
#include "ui_language.h"
#include "ui_scale.h"
#include "vector_icons.h"
#include "workflow_queue.h"
#include <QJsonArray>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QtWidgets>
#include <functional>
#include <algorithm>
#include <QHelpEvent>
#include <QtMath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <wininet.h>
#endif

namespace
{
class GroupCheck : public QCheckBox
{
  public:
    explicit GroupCheck(QWidget *parent) : QCheckBox(parent) { setTristate(true); }

  protected:
    void nextCheckState() Q_DECL_OVERRIDE
    {
        setCheckState(checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    }
};
int groupCandidate(const DriverCatalog *catalog, int row, int preferred = -1)
{
    if (row < 0 || row >= catalog->size() || catalog->isIgnored(row))
        return -1;
    const auto &entry = catalog->entryRef(row);
    auto eligible = [&](int index)
    {
        return index > 0 && index < entry.candidates.size() && entry.candidates[index].valid &&
               (!entry.candidates[index].local || catalog->canQueue(row));
    };
    if (preferred == 0)
        preferred = reinstallCandidateIndex(entry);
    if (eligible(preferred))
        return preferred;
    int first = -1;
    for (int index = 1; index < entry.candidates.size(); ++index)
        if (eligible(index))
        {
            if (first < 0)
                first = index;
            if (entry.candidates[index].recommended)
                return index;
        }
    return first;
}
class ClickRow : public QFrame
{
  public:
    std::function<void()> clicked, doubled;
    QTimer *clickTimer;
    bool suppressRelease = false;
    explicit ClickRow(QWidget *p) : QFrame(p)
    {
        clickTimer = new QTimer(this);
        clickTimer->setSingleShot(true);
        connect(clickTimer, &QTimer::timeout, this,
                [this]()
                {
                    if (clicked)
                        clicked();
                });
        setObjectName(QStringLiteral("drpRow"));
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }

  protected:
    void mouseReleaseEvent(QMouseEvent *e) Q_DECL_OVERRIDE
    {
        if (suppressRelease)
        {
            suppressRelease = false;
            return;
        }
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && clicked)
        {
            clicked();
        }
        QFrame::mouseReleaseEvent(e);
    }
    void mouseDoubleClickEvent(QMouseEvent *e) Q_DECL_OVERRIDE
    {
        if (e->button() == Qt::LeftButton && doubled)
        {
            clickTimer->stop();
            suppressRelease = true;
            if (clicked)
                clicked(); // Restore the first-click selection before opening information.
            doubled();
        }
    }
};
class ProjectTableHeader : public QHeaderView
{
  public:
    explicit ProjectTableHeader(QWidget *parent) : QHeaderView(Qt::Horizontal, parent)
    {
        setMinimumHeight(50);
        setDefaultAlignment(Qt::AlignCenter);
        setMouseTracking(true);
        viewport()->setAutoFillBackground(false);
        setToolTip(UiLanguage::text(QStringLiteral("Drag a column border to resize"),
                                    QString::fromUtf8("اسحب حدود الأعمدة لتغيير حجمها")));
    }

  protected:
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF face = QRectF(viewport()->rect()).adjusted(0, 0, -1, -1);
        QPainterPath clip;
        clip.addRoundedRect(face, 12, 12);
        painter.setClipPath(clip);
        QLinearGradient fill(face.topLeft(), face.topRight());
        fill.setColorAt(0, Theme::color("headerStart"));
        fill.setColorAt(1, Theme::color("headerEnd"));
        painter.fillRect(face, fill);
        QFont heading = font();
        heading.setPixelSize(13);
        heading.setWeight(QFont::DemiBold);
        painter.setFont(heading);
        for (int visual = 0; visual < count(); ++visual)
        {
            const int section = logicalIndex(visual);
            if (isSectionHidden(section))
                continue;
            QRect area(sectionViewportPosition(section), 0, sectionSize(section), height());
            if (!area.intersects(viewport()->rect()))
                continue;
            if (underMouse() && area.contains(viewport()->mapFromGlobal(QCursor::pos())))
                painter.fillRect(area, QColor(255, 255, 255, 12));
            painter.setPen(QColor(255, 255, 255, 22));
            painter.drawLine(area.right(), 12, area.right(), height() - 12);
            const QString label =
                model() ? model()->headerData(section, Qt::Horizontal).toString() : QString();
            painter.setPen(QColor(QStringLiteral("#edfbfb")));
            painter.drawText(
                area.adjusted(14, 0, -14, 0), Qt::AlignCenter,
                painter.fontMetrics().elidedText(label, Qt::ElideRight, qMax(0, area.width() - 28)));
        }
    }
};
class TableDelegate : public QStyledItemDelegate
{
  public:
    explicit TableDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const Q_DECL_OVERRIDE
    {
        auto clean = option;
        clean.state &= ~(QStyle::State_HasFocus | QStyle::State_Selected | QStyle::State_MouseOver);
        clean.backgroundBrush = Qt::NoBrush;
        QStyledItemDelegate::paint(painter, clean, index);
    }
};
class ProjectTable : public QTreeWidget
{
  public:
    explicit ProjectTable(QWidget *parent) : QTreeWidget(parent)
    {
        setProperty("projectRecords", true);
        setHeader(new ProjectTableHeader(this));
        setItemDelegate(new TableDelegate(this));
        setSelectionBehavior(QAbstractItemView::SelectRows);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QWidget::customContextMenuRequested, this,
                [this](const QPoint &point)
                {
                    const QModelIndex index = indexAt(point);
                    if (!index.isValid())
                        return;
                    setCurrentIndex(index);
                    ProjectMenu menu(this);
                    auto value = menu.addAction(
                        UiLanguage::text(QStringLiteral("Copy value"), QString::fromUtf8("نسخ القيمة")));
                    auto row = menu.addAction(
                        UiLanguage::text(QStringLiteral("Copy row"), QString::fromUtf8("نسخ السطر")));
                    connect(value, &QAction::triggered, this,
                            [index]() { QApplication::clipboard()->setText(index.data().toString()); });
                    connect(row, &QAction::triggered, this,
                            [this, index]()
                            {
                                QStringList values;
                                for (int i = 0; i < columnCount(); ++i)
                                    values << index.sibling(index.row(), i).data().toString();
                                QApplication::clipboard()->setText(values.join(QLatin1Char('\t')));
                            });
                    menu.exec(viewport()->mapToGlobal(point));
                });
        connect(this, &QTreeWidget::itemChanged, this, [this]() { viewport()->update(); });
        setUniformRowHeights(true);
        setMouseTracking(true);
        viewport()->setMouseTracking(true);
        setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

  protected:
    bool viewportEvent(QEvent *event) Q_DECL_OVERRIDE
    {
        if (event->type() == QEvent::ToolTip)
        {
            auto help = static_cast<QHelpEvent *>(event);
            const QModelIndex index = indexAt(help->pos());
            if (index.isValid())
            {
                viewport()->setToolTip(index.data(Qt::ToolTipRole).toString().isEmpty()
                                           ? index.data().toString()
                                           : index.data(Qt::ToolTipRole).toString());
                QHelpEvent projectHelp(QEvent::ToolTip, help->pos(), help->globalPos());
                QApplication::sendEvent(viewport(), &projectHelp);
                viewport()->setToolTip(QString());
            }
            event->accept();
            return true;
        }
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave)
            viewport()->update();
        return QTreeWidget::viewportEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) Q_DECL_OVERRIDE
    {
        const QModelIndex index = indexAt(event->pos());
        if (event->button() == Qt::LeftButton && index.isValid())
        {
            setCurrentIndex(index);
            QApplication::clipboard()->setText(index.data().toString());
            event->accept();
            return;
        }
        QTreeWidget::mouseDoubleClickEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE
    {
        if (event->matches(QKeySequence::Copy) && currentIndex().isValid())
        {
            QApplication::clipboard()->setText(currentIndex().data().toString());
            event->accept();
            return;
        }
        QTreeWidget::keyPressEvent(event);
    }
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option,
                 const QModelIndex &index) const Q_DECL_OVERRIDE
    {
        const bool selected = selectionModel()->isRowSelected(index.row(), index.parent()) ||
                              index.sibling(index.row(), 0).data(Qt::CheckStateRole).toInt() == Qt::Checked;
        const bool hovered = option.rect.contains(viewport()->mapFromGlobal(QCursor::pos()));
        QRectF face(1, option.rect.top() + 2, viewport()->width() - 3, option.rect.height() - 4);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        const bool dark = Theme::isDark(this);
        if (selected || hovered)
        {
            painter->setBrush(selected ? (dark ? QColor("#175c72") : QColor("#cdece8"))
                                       : (dark ? QColor("#294e60") : QColor("#edf7f8")));
            painter->drawRoundedRect(face, 10, 10);
        }
        else
        {
            painter->setPen(QPen(Theme::color("border", dark), .6));
            painter->drawLine(QPointF(12, option.rect.bottom()),
                              QPointF(viewport()->width() - 12, option.rect.bottom()));
        }
        painter->restore();
        auto clean = option;
        clean.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        clean.features &= ~QStyleOptionViewItem::Alternate;
        QTreeWidget::drawRow(painter, clean, index);
    }
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE
    {
        QTreeWidget::resizeEvent(event);
        header()->setStretchLastSection(true);
        if (property("diagnosticTable").toBool())
        {
            header()->setStretchLastSection(false);
            const int available = viewport()->width();
            QVector<int> wanted;
            int total = 0;
            for (int col = 0; col < columnCount(); ++col)
            {
                int ideal = fontMetrics().width(headerItem()->text(col)) + 32;
                for (int row = 0; row < qMin(40, topLevelItemCount()); ++row)
                    ideal = qMax(ideal, fontMetrics().width(topLevelItem(row)->text(col)) + 32);
                ideal = qBound(90, ideal, 380);
                wanted << ideal;
                total += ideal;
            }
            int used = 0;
            for (int col = 0; col < columnCount(); ++col)
            {
                const int size = col == columnCount() - 1 ? qMax(90, available - used)
                                                          : (total ? available * wanted[col] / total
                                                                   : available / qMax(1, columnCount()));
                setColumnWidth(col, size);
                used += size;
            }
        }
        if (columnCount() == 3 && !property("diagnosticTable").toBool())
        {
            const int available = viewport()->width();
            header()->setSectionResizeMode(QHeaderView::Interactive);
            header()->setStretchLastSection(false);
            setColumnWidth(0, qMax(180, int(available * .48)));
            setColumnWidth(1, qMax(105, int(available * .22)));
            setColumnWidth(2, qMax(130, available - columnWidth(0) - columnWidth(1)));
        }
    }
};
class WelcomeCard : public SurfaceCard
{
  public:
    explicit WelcomeCard(QWidget *parent) : SurfaceCard(parent) {}

  protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE
    {
        SurfaceCard::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF face = QRectF(rect()).adjusted(1, 1, -1, -3);
        QPainterPath clip;
        clip.addRoundedRect(face, 20, 20);
        painter.setClipPath(clip);
        const QRectF banner(face.left(), face.top(), face.width(), 218);
        QLinearGradient fill(banner.topLeft(), banner.topRight());
        fill.setColorAt(0, QColor("#064d72"));
        fill.setColorAt(.6, QColor("#096b78"));
        fill.setColorAt(1, QColor("#07877d"));
        painter.fillRect(banner, fill);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(133, 245, 232, 18), 1.2));
        for (int i = 0; i < 4; ++i)
            painter.drawEllipse(QPointF(banner.right() - 35, banner.top() + 20), 90 + i * 48, 90 + i * 48);
        painter.setPen(QPen(QColor(255, 255, 255, 14), 1));
        painter.drawLine(QPointF(banner.left(), banner.bottom()), banner.bottomRight());
    }
};
class ContentLoader : public QWidget
{
    QTimer tick;
    int phase = 0;
    QString title, detail;
    QPixmap backdrop;

  public:
    ContentLoader(QWidget *parent, const QString &heading, const QString &caption)
        : QWidget(parent), title(heading), detail(caption)
    {
        setObjectName(QStringLiteral("contentLoader"));
        setAttribute(Qt::WA_StyledBackground, false);
        parent->installEventFilter(this);
        setGeometry(parent->rect());
        hide(); // Exclude this cover from the one-time parent snapshot.
        parent->ensurePolished();
        if (parent->layout())
            parent->layout()->activate();
        const qreal dpr = parent->devicePixelRatioF();
        QImage source(QSize(qCeil(parent->width() * dpr), qCeil(parent->height() * dpr)),
                      QImage::Format_ARGB32_Premultiplied);
        source.setDevicePixelRatio(dpr);
        source.fill(Theme::color("background", Theme::isDark(parent)));
        {
            QPainter capture(&source);
            parent->render(&capture, QPoint(), QRegion(), QWidget::DrawChildren);
        }
        const auto small = source.scaled(qMax(1, source.width() / 10), qMax(1, source.height() / 10),
                                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        backdrop =
            QPixmap::fromImage(small.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        setProperty("snapshotBackdrop", !backdrop.isNull());
        tick.setInterval(33);
        connect(&tick, &QTimer::timeout, this,
                [this]()
                {
                    phase = (phase + 1) % 60;
                    update();
                });
        show();
        raise();
        tick.start();
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) Q_DECL_OVERRIDE
    {
        if (object == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        {
            setGeometry(parentWidget()->rect());
            raise();
        }
        return false;
    }
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (!backdrop.isNull())
            painter.drawPixmap(rect(), backdrop);
        painter.fillRect(rect(), QColor(5, 22, 35, 177));
        const QPointF center(width() / 2., height() / 2. - 38.);
        for (int i = 0; i < 6; ++i)
        {
            const qreal angle = phase * .105 - i * .39 - 1.5707963267948966;
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(118, 239, 231, qBound(65, 240 - i * 29, 255)));
            const qreal radius = 3.1 + (5 - i) * .17;
            painter.drawEllipse(QPointF(center.x() + 34 * qCos(angle), center.y() + 34 * qSin(angle)), radius,
                                radius);
        }
        QFont heading = font();
        heading.setPixelSize(29);
        heading.setWeight(QFont::DemiBold);
        painter.setFont(heading);
        painter.setPen(Qt::white);
        painter.drawText(QRect(24, height() / 2 + 54, width() - 48, 44), Qt::AlignCenter, title);
        heading.setPixelSize(16);
        heading.setWeight(QFont::Normal);
        painter.setFont(heading);
        painter.drawText(QRect(32, height() / 2 + 104, width() - 64, 60), Qt::AlignHCenter | Qt::TextWordWrap,
                         detail);
        heading.setPixelSize(12);
        painter.setFont(heading);
        painter.drawText(QRect(24, height() - 58, width() - 48, 26), Qt::AlignCenter,
                         UiLanguage::text(QStringLiteral("DriverPack Modern | UI: Tarek Sadek"),
                                          QString::fromUtf8("درايفر باك مودرن | واجهة طارق صادق")));
    }
};
class PillTabBar : public QTabBar
{
    QVariantAnimation slide;
    QRectF highlight;
    QRectF face(int index) const { return QRectF(tabArea(index)).adjusted(4, 5, -4, -7); }

  public:
    explicit PillTabBar(QWidget *parent) : QTabBar(parent), slide(this)
    {
        setDrawBase(false);
        setExpanding(true);
        setUsesScrollButtons(false);
        setFixedHeight(62);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        slide.setDuration(190);
        slide.setEasingCurve(QEasingCurve::OutCubic);
        connect(&slide, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value)
                {
                    highlight = value.toRectF();
                    update();
                });
        connect(this, &QTabBar::currentChanged, this,
                [this](int index)
                {
                    if (index < 0)
                        return;
                    const QRectF destination = face(index);
                    slide.stop();
                    if (!isVisible() || highlight.isEmpty() || qApp->property("reduceMotion").toBool())
                    {
                        highlight = destination;
                        update();
                        return;
                    }
                    slide.setStartValue(highlight);
                    slide.setEndValue(destination);
                    slide.start();
                });
    }
    bool isTransitioning() const { return slide.state() == QAbstractAnimation::Running; }
    QRect tabArea(int index) const
    {
        if (index < 0 || index >= count())
            return QRect();
        const int visual = layoutDirection() == Qt::RightToLeft ? count() - 1 - index : index;
        const int left = width() * visual / qMax(1, count()),
                  right = width() * (visual + 1) / qMax(1, count());
        return QRect(left, 0, right - left, height());
    }

  protected:
    bool event(QEvent *event) Q_DECL_OVERRIDE
    {
        if (event->type() == QEvent::ToolTip)
        {
            auto help = static_cast<QHelpEvent *>(event);
            for (int index = 0; index < count(); ++index)
                if (tabArea(index).contains(help->pos()))
                {
                    setToolTip(tabToolTip(index).isEmpty() ? tabText(index) : tabToolTip(index));
                    QHelpEvent projectHelp(QEvent::ToolTip, help->pos(), help->globalPos());
                    QApplication::sendEvent(this, &projectHelp);
                    setToolTip(QString());
                    event->accept();
                    return true;
                }
            return true;
        }
        return QTabBar::event(event);
    }
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE
    {
        if (event->button() == Qt::LeftButton)
        {
            for (int index = 0; index < count(); ++index)
                if (tabArea(index).contains(event->pos()) && isTabEnabled(index))
                {
                    setCurrentIndex(index);
                    setFocus(Qt::MouseFocusReason);
                    event->accept();
                    return;
                }
        }
        QTabBar::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) Q_DECL_OVERRIDE
    {
        if (event->button() == Qt::LeftButton)
        {
            event->accept();
            return;
        }
        QTabBar::mouseReleaseEvent(event);
    }
    QSize tabSizeHint(int) const Q_DECL_OVERRIDE { return QSize(qMax(1, width() / qMax(1, count())), 62); }
    QSize minimumTabSizeHint(int index) const Q_DECL_OVERRIDE { return tabSizeHint(index); }
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE
    {
        QTabBar::resizeEvent(event);
        slide.stop();
        highlight = face(currentIndex());
    }
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE
    {
        QTabBar::showEvent(event);
        highlight = face(currentIndex());
    }
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = Theme::isDark(this), rtl = layoutDirection() == Qt::RightToLeft;
        for (int i = 0; i < count(); ++i)
        {
            const QRectF area = face(i);
            const bool hover = area.contains(mapFromGlobal(QCursor::pos()));
            painter.setPen(QPen(Theme::color("border", dark), 1));
            painter.setBrush(Theme::color(hover ? "elevated" : "surface", dark));
            painter.drawRoundedRect(area, 13, 13);
        }
        if (!highlight.isEmpty())
        {
            painter.setPen(QPen(Theme::color("accent", dark), 1));
            painter.setBrush(dark ? QColor("#225261") : QColor("#d9eeec"));
            painter.drawRoundedRect(highlight, 13, 13);
        }
        for (int i = 0; i < count(); ++i)
        {
            const QRectF area = face(i);
            QFont label = font();
            label.setPixelSize(count() > 4 ? 13 : 14);
            label.setWeight(i == currentIndex() ? QFont::DemiBold : QFont::Normal);
            painter.setFont(label);
            painter.setPen(Theme::color(i == currentIndex() ? "text" : "muted", dark));
            const int gap = count() > 4 ? 10 : 15;
            const QRect iconRect(rtl ? area.right() - gap - 20 : area.left() + gap, area.center().y() - 10,
                                 20, 20);
            tabIcon(i).paint(&painter, iconRect);
            const QRectF textArea = area.adjusted(rtl ? 8 : gap + 25, 0, rtl ? -gap - 25 : -8, 0);
            painter.drawText(
                textArea, Qt::AlignCenter,
                painter.fontMetrics().elidedText(tabText(i), Qt::ElideRight, qMax(1, int(textArea.width()))));
        }
    }
};
class SettingsTabs : public QTabWidget
{
    void fitBar()
    {
        tabBar()->setFixedWidth(width());
        tabBar()->setGeometry(0, 0, width(), 62);
    }

  public:
    explicit SettingsTabs(QWidget *parent) : QTabWidget(parent)
    {
        setTabBar(new PillTabBar(this));
        setDocumentMode(true);
        setAutoFillBackground(false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    PillTabBar *navigation() const { return static_cast<PillTabBar *>(tabBar()); }
    QSize minimumSizeHint() const Q_DECL_OVERRIDE
    {
        return QSize(320, QTabWidget::minimumSizeHint().height());
    }

  protected:
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE
    {
        QTabWidget::resizeEvent(event);
        fitBar();
    }
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE
    {
        QTabWidget::showEvent(event);
        fitBar();
    }
};
class DriverChevron : public QAbstractButton
{
    QVariantAnimation animation;
    qreal amount = 0;

  public:
    DriverChevron(bool open, QWidget *parent) : QAbstractButton(parent), animation(this), amount(open ? 1 : 0)
    {
        setProperty("ownHover", true);
        setFixedSize(32, 36);
        setCursor(Qt::PointingHandCursor);
        animation.setDuration(180);
        animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v)
                {
                    amount = v.toReal();
                    update();
                });
    }
    void setOpen(bool open)
    {
        animation.stop();
        animation.setStartValue(amount);
        animation.setEndValue(open ? 1. : 0.);
        animation.start();
    }

  protected:
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(rect().center());
        painter.rotate(layoutDirection() == Qt::RightToLeft ? 180 - 90 * amount : 90 * amount);
        VectorIcons::paint(&painter, QStringLiteral("chevron-right"), QRectF(-9, -9, 18, 18),
                           Theme::color("accent", Theme::isDark(this)));
    }
};
class DriverAlternatives : public QWidget
{
    QVariantAnimation animation;
    bool populated = false;
    bool opened = false;
    bool preparing = false;
    QWidget *content;
    int stableContentHeight = 0;
    int stableWidth = -1;
    int contentHeight() const
    {
        auto box = content->layout();
        return box ? (box->hasHeightForWidth() ? box->totalHeightForWidth(width()) : box->sizeHint().height())
                   : 0;
    }
    void placeContent()
    {
        if (stableWidth != width() || !isAnimating())
        {
            stableContentHeight = contentHeight();
            stableWidth = width();
        }
        content->setGeometry(0, 0, width(), stableContentHeight);
    }

  public:
    std::function<void()> populate;
    explicit DriverAlternatives(QWidget *parent)
        : QWidget(parent), animation(this), content(new QWidget(this))
    {
        setObjectName(QStringLiteral("driverAlternatives"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(0);
        animation.setDuration(200);
        animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v) { setFixedHeight(v.toInt()); });
        connect(&animation, &QVariantAnimation::finished, this,
                [this]()
                {
                    if (!opened)
                        hide();
                });
    }
    QWidget *canvas() const { return content; }
    bool isAnimating() const { return animation.state() == QAbstractAnimation::Running; }

  protected:
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE
    {
        QWidget::resizeEvent(event);
        placeContent();
        if (opened && !preparing && !isAnimating() && height() != contentHeight())
            setFixedHeight(contentHeight());
    }

  public:
    void setOpen(bool open, bool motion)
    {
        QScopedValueRollback<bool> preparation(preparing, true);
        animation.stop();
        opened = open;
        if (parentWidget() && parentWidget()->layout())
        {
            const auto margins = parentWidget()->layout()->contentsMargins();
            resize(qMax(0, parentWidget()->contentsRect().width() - margins.left() - margins.right()),
                   height());
        }
        if (open && !populated)
        {
            populated = true;
            if (populate)
                populate();
        }
        // Animate a clipping viewport, never the layout's own minimum height.
        // Child rows retain their full geometry throughout the transition.
        placeContent();
        const int target = open ? contentHeight() : 0;
        if (!motion || qApp->property("reduceMotion").toBool())
        {
            setFixedHeight(target);
            setVisible(open);
            return;
        }
        animation.setStartValue(height());
        animation.setEndValue(target);
        animation.start();
        show();
    }
};
class DiagnosticGrid : public QWidget
{
    QGridLayout *grid;
    QVector<QWidget *> cards;
    int columns = 0;
    void arrange()
    {
        const int next = property("settingsGrid").toBool() ? (width() >= 1200  ? 3
                                                              : width() >= 740 ? 2
                                                                               : 1)
                                                           : (width() >= 700 ? 2 : 1);
        if (next == columns)
            return;
        columns = next;
        while (auto item = grid->takeAt(0))
            delete item;
        for (int i = 0; i < cards.size(); ++i)
            grid->addWidget(cards[i], i / columns, i % columns);
    }

  public:
    explicit DiagnosticGrid(QWidget *parent) : QWidget(parent), grid(new QGridLayout(this))
    {
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(16);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    void addCard(QWidget *card)
    {
        cards << card;
        columns = 0;
        arrange();
    }

  protected:
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE
    {
        QWidget::resizeEvent(event);
        arrange();
    }
};
class DragHeader : public QWidget
{
    QPoint anchor;
    bool moving = false;

  public:
    explicit DragHeader(QWidget *p) : QWidget(p) {}

  protected:
    void mousePressEvent(QMouseEvent *e) Q_DECL_OVERRIDE
    {
        if (e->button() == Qt::LeftButton)
        {
            anchor = e->globalPos() - window()->frameGeometry().topLeft();
            moving = true;
        }
    }
    void mouseMoveEvent(QMouseEvent *e) Q_DECL_OVERRIDE
    {
        if (moving && !window()->isMaximized())
            window()->move(e->globalPos() - anchor);
    }
    void mouseReleaseEvent(QMouseEvent *) Q_DECL_OVERRIDE { moving = false; }
    void mouseDoubleClickEvent(QMouseEvent *) Q_DECL_OVERRIDE
    {
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
    }
};
class DrpSwitch : public QCheckBox
{
    bool userGesture = false;
    bool controlDark = false;
    ToggleSwitch *control;

  public:
    explicit DrpSwitch(const QString &text, QWidget *parent)
        : QCheckBox(text, parent), control(new ToggleSwitch(this))
    {
        setMinimumHeight(28);
        setToolTip(text);
        setAccessibleName(text);
        setCursor(Qt::PointingHandCursor);
        control->setAttribute(Qt::WA_TransparentForMouseEvents);
        control->setFocusPolicy(Qt::NoFocus);
        control->setMotionEnabled(false);
        connect(this, &QCheckBox::toggled, this,
                [this](bool value)
                {
                    control->setMotionEnabled(userGesture && isVisible() &&
                                              !qApp->property("reduceMotion").toBool());
                    control->setChecked(value);
                    update();
                });
    }
    bool isAnimating() const
    {
        auto animation = control->findChild<QPropertyAnimation *>();
        return animation && animation->state() == QAbstractAnimation::Running;
    }
    QSize sizeHint() const Q_DECL_OVERRIDE
    {
        return QSize(62 + fontMetrics().width(text()), qMax(28, fontMetrics().height() + 6));
    }

  protected:
    void nextCheckState() Q_DECL_OVERRIDE
    {
        userGesture = true;
        QCheckBox::nextCheckState();
        userGesture = false;
    }
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE
    {
        control->setMotionEnabled(false);
        control->setChecked(isChecked());
        control->setPosition(isChecked() ? 1. : 0.);
        QCheckBox::showEvent(event);
    }
    bool hitButton(const QPoint &point) const Q_DECL_OVERRIDE { return rect().contains(point); }
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        const bool dark = Theme::isDark(this);
        if (dark != controlDark)
        {
            controlDark = dark;
            control->setDark(dark);
        }
        control->move(rtl ? width() - control->width() : 0, (height() - control->height()) / 2);
        QPainter painter(this);
        painter.setFont(font());
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect().adjusted(rtl ? 0 : 62, 0, rtl ? -62 : 0, 0),
                         Qt::AlignVCenter | (rtl ? Qt::AlignRight : Qt::AlignLeft), text());
    }
};
class HeroButton : public QPushButton
{
    QVariantAnimation hover;
    qreal amount = 0;
    void animate(qreal target)
    {
        hover.stop();
        hover.setStartValue(amount);
        hover.setEndValue(target);
        hover.start();
    }

  public:
    HeroButton(const QString &text, QWidget *parent) : QPushButton(text, parent), hover(this)
    {
        setFixedHeight(42);
        setCursor(Qt::PointingHandCursor);
        setToolTip(text);
        hover.setDuration(150);
        hover.setEasingCurve(QEasingCurve::OutCubic);
        connect(&hover, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value)
                {
                    amount = value.toReal();
                    update();
                });
    }
    QSize sizeHint() const Q_DECL_OVERRIDE
    {
        return QSize(fontMetrics().width(text()) + 44 + (icon().isNull() ? 0 : iconSize().width() + 8), 42);
    }

  protected:
    void enterEvent(QEvent *event) Q_DECL_OVERRIDE
    {
        animate(1);
        QPushButton::enterEvent(event);
    }
    void leaveEvent(QEvent *event) Q_DECL_OVERRIDE
    {
        animate(0);
        QPushButton::leaveEvent(event);
    }
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (!isEnabled())
            painter.setOpacity(.45);
        painter.setPen(QPen(QColor(255, 255, 255, qRound(70 + amount * 80)), 1));
        painter.setBrush(QColor(255, 255, 255, qRound(15 + amount * 35 + (isDown() ? 20 : 0))));
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 12, 12);
        QFont face = font();
        face.setPixelSize(14);
        painter.setFont(face);
        painter.setPen(Qt::white);
        const int iconWidth = icon().isNull() ? 0 : 20;
        const int textWidth = painter.fontMetrics().width(text());
        const int total = textWidth + (iconWidth ? iconWidth + 8 : 0);
        const int start = (width() - total) / 2;
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        if (iconWidth)
            icon().paint(&painter, QRect(rtl ? start + textWidth + 8 : start, (height() - 20) / 2, 20, 20));
        painter.drawText(QRect(start + (!rtl && iconWidth ? 28 : 0), 0, textWidth, height()), Qt::AlignCenter,
                         text());
    }
};
class DrpNavButton : public QPushButton
{
    QVariantAnimation *hoverAnimation;
    qreal hoverAmount = 0;

  public:
    explicit DrpNavButton(const QString &text, QWidget *parent) : QPushButton(text, parent)
    {
        setMouseTracking(true);
        hoverAnimation = new QVariantAnimation(this);
        hoverAnimation->setDuration(140);
        hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(hoverAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value)
                {
                    hoverAmount = value.toReal();
                    update();
                });
    }

  protected:
    void enterEvent(QEvent *event) Q_DECL_OVERRIDE
    {
        hoverAnimation->stop();
        hoverAnimation->setStartValue(hoverAmount);
        hoverAnimation->setEndValue(1.0);
        hoverAnimation->start();
        QPushButton::enterEvent(event);
    }
    void leaveEvent(QEvent *event) Q_DECL_OVERRIDE
    {
        hoverAnimation->stop();
        hoverAnimation->setStartValue(hoverAmount);
        hoverAnimation->setEndValue(0.0);
        hoverAnimation->start();
        QPushButton::leaveEvent(event);
    }
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor base(QStringLiteral("#07517e"));
        if (window()->property("nativeCustomTheme").toBool())
            base = Theme::color("sidebarStart");
        painter.setPen(Qt::NoPen);
        painter.setBrush(base);
        painter.drawRoundedRect(rect().adjusted(4, 3, -4, -3), 14, 14);
        if (isChecked() || hoverAmount > 0)
        {
            painter.setBrush(isChecked() ? QColor(QStringLiteral("#117caf"))
                                         : QColor(255, 255, 255, qRound(28 * hoverAmount)));
            painter.drawRoundedRect(rect().adjusted(8, 5, -8, -5), 13, 13);
        }
        if (isChecked())
        {
            painter.setBrush(QColor(QStringLiteral("#83efdf")));
            painter.drawRoundedRect(
                QRectF(layoutDirection() == Qt::RightToLeft ? width() - 5 : 1, 18, 4, height() - 36), 2, 2);
        }
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        const int x = text().isEmpty() ? (width() - 24) / 2 : rtl ? width() - 42 : 18;
        VectorIcons::icon(property("drpNavigationIcon").toString(),
                          isChecked() ? Qt::white : QColor(QStringLiteral("#bad2e0")), 24)
            .paint(&painter, QRect(x, (height() - 24) / 2, 24, 24));
        QFont face = font();
        face.setPixelSize(17);
        face.setWeight(isChecked() ? QFont::DemiBold : QFont::Normal);
        while (face.pixelSize() > 14 && QFontMetrics(face).width(text()) > width() - 66)
            face.setPixelSize(face.pixelSize() - 1);
        painter.setFont(face);
        painter.setPen(isChecked() ? Qt::white : QColor(QStringLiteral("#bad2e0")));
        painter.drawText(rect().adjusted(rtl ? 12 : 54, 0, rtl ? -54 : -12, 0),
                         Qt::AlignVCenter | Qt::AlignAbsolute | (rtl ? Qt::AlignRight : Qt::AlignLeft),
                         text());
    }
};
bool offlineCategoryEnabled(QSettings &settings, const QString &category)
{
    const QMap<QString, int> index = {
        {QStringLiteral("Chipset"), 1}, {QStringLiteral("Monitor"), 2}, {QStringLiteral("TV tuner"), 3},
        {QStringLiteral("Video"), 4},   {QStringLiteral("Network"), 5}, {QStringLiteral("Printer"), 6},
        {QStringLiteral("Camera"), 7},  {QStringLiteral("Other"), 8},   {QStringLiteral("Storage"), 9},
        {QStringLiteral("Audio"), 10},  {QStringLiteral("Wi-Fi"), 11},  {QStringLiteral("Card reader"), 12},
        {QStringLiteral("Modem"), 13},  {QStringLiteral("Input"), 14}};
    return category == QStringLiteral("Indexes") ||
           settings.value(QStringLiteral("offline/category%1").arg(index.value(category, 8)), true).toBool();
}
QString bytes(qint64 n)
{
    if (n >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(n / 1073741824.0, 0, 'f', 2);
    if (n >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(n / 1048576.0, 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(n / 1024.0, 0, 'f', 1);
}
QString ltr(const QString &s) { return QString(QChar(0x202a)) + s + QChar(0x202c); }
QString deviceIcon(const QString &value)
{
    const QString s = value.toLower();
    if (s.contains(QStringLiteral("bluetooth")))
        return QStringLiteral("bluetooth");
    if (s.contains(QStringLiteral("keyboard")))
        return QStringLiteral("keyboard");
    if (s.contains(QStringLiteral("mouse")) || s.contains(QStringLiteral("hidclass")))
        return QStringLiteral("mouse");
    if (s.contains(QStringLiteral("printer")))
        return QStringLiteral("printer");
    if (s.contains(QStringLiteral("battery")))
        return QStringLiteral("battery");
    if (s.contains(QStringLiteral("usb")))
        return QStringLiteral("usb");
    if (s.contains(QStringLiteral("display")))
        return QStringLiteral("monitor");
    if (s.contains(QStringLiteral("media")) || s.contains(QStringLiteral("audio")))
        return QStringLiteral("audio");
    if (s.contains(QStringLiteral("net")))
        return QStringLiteral("network");
    if (s.contains(QStringLiteral("camera")) || s.contains(QStringLiteral("image")))
        return QStringLiteral("camera");
    if (s.contains(QStringLiteral("disk")) || s.contains(QStringLiteral("storage")))
        return QStringLiteral("storage");
    return QStringLiteral("chip");
}
void events(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, SLOT(quit()));
    loop.exec();
    QCoreApplication::sendPostedEvents(0, QEvent::DeferredDelete);
}
} // namespace

QString DriverPackShell::t(const char *en, const char *ar) const
{
    return UiLanguage::text(QString::fromUtf8(en), QString::fromUtf8(ar));
}
QLabel *DriverPackShell::copy(const QString &s, QWidget *p, const char *role)
{
    auto l = new QLabel(s, p);
    l->setTextFormat(Qt::PlainText);
    l->setWordWrap(true);
    l->setObjectName(QString::fromLatin1(role));
    return l;
}
QPushButton *DriverPackShell::action(const QString &s, QWidget *p, bool primary)
{
    auto b = !primary && dynamic_cast<AccentCard *>(p) ? static_cast<QPushButton *>(new HeroButton(s, p))
                                                       : new QPushButton(s, p);
    b->setObjectName(primary ? QStringLiteral("drpPrimary") : QStringLiteral("drpAction"));
    b->setProperty("projectAction", !primary);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(s);
    b->setAccessibleName(s);
    b->setMinimumHeight(primary ? 64 : 38);
    return b;
}
QWidget *DriverPackShell::pageBody(QWidget *page, QVBoxLayout **layout)
{
    auto host = new QVBoxLayout(page);
    host->setContentsMargins(0, 0, 0, 0);
    auto scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("drpPageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("drpBody"));
    *layout = new QVBoxLayout(body);
    (*layout)->setContentsMargins(34, 30, 34, 36);
    (*layout)->setSpacing(24);
    scroll->setWidget(body);
    host->addWidget(scroll);
    return body;
}
DriverPackShell::DriverPackShell(bool demo, bool capture, QWidget *parent)
    : QWidget(parent), m_demo(demo), m_capture(capture)
{
    qApp->setProperty("reduceMotion", false);
    setObjectName(QStringLiteral("driverPackShell"));
    setProperty("readOnlyPreview", m_capture || m_demo);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(560, 360);
    resize(640, 440);
    m_settings = createAppSettings(QString(), this, false);
    m_mode = m_settings->value(QStringLiteral("driverpack/defaultRecommendations"), false).toBool() ? 2 : 0;
    m_language = m_settings->value(QStringLiteral("language"), QStringLiteral("english")).toString();
    UiLanguage::setLanguage(m_language);
    m_arabic = m_language == QStringLiteral("arabic");
    m_themeMode = m_settings->value(QStringLiteral("theme/mode"), QStringLiteral("system")).toString();
    const QString pack = m_settings->value(QStringLiteral("theme/nativePack")).toString();
    if (!pack.isEmpty())
        try
        {
            Theme::setDocument(Theme::readPack(pack));
        }
        catch (...)
        {
        }
    m_library = QCoreApplication::applicationDirPath();
    const QStringList args = QCoreApplication::arguments();
    const int arg = args.indexOf(QStringLiteral("--library"));
    if (arg >= 0)
        m_library = QDir(args.value(arg + 1)).absolutePath();
    const QString custom = m_settings->value(QStringLiteral("driverpack/library")).toString();
    if (arg < 0 && !custom.isEmpty())
        m_library = custom;
    m_engine = new EngineBridge(this);
    m_catalog = new DriverCatalog(this);
    m_hardware = new HardwareWorker(this);
    m_setupTimer = new QTimer(this);
    m_setupTimer->setInterval(400);
    m_clock = new QTimer(this);
    m_clock->setInterval(1000);
    m_root = 0;
    m_primary = 0;
    m_footerPrimary = 0;
    m_softwarePrimary = 0;
    m_elapsedText = 0;
    m_installText = 0;
    m_installItems = 0;
    m_installBar = 0;
    connect(m_engine, &EngineBridge::inventoryReady, this,
            [this](const QVector<DeviceInfo> &devices, const QString &message)
            {
                if (!m_demo)
                    return;
                m_catalog->replace(devices, true);
                m_model = message;
                loadSoftware();
                m_initial = false;
                startPage();
                m_root->setCurrentWidget(m_start);
            });
    connect(m_engine, &EngineBridge::nativeSnapshotReady, this,
            [this](const sdiomodern::Snapshot &s) { snapshot(s); });
    connect(m_engine, &EngineBridge::nativeProgress, this,
            [this](const sdiomodern::Event &e) { nativeProgress(e); });
    connect(m_engine, &EngineBridge::nativeInstallationFinished, this,
            [this](const sdiomodern::InstallReport &r) { nativeFinished(r); });
    connect(m_engine, &EngineBridge::engineError, this,
            [this](const QString &e)
            {
                m_error = e;
                for (auto overlay : m_pages->findChildren<QWidget *>(QStringLiteral("contentLoader"),
                                                                     Qt::FindDirectChildrenOnly))
                {
                    overlay->hide();
                    overlay->deleteLater();
                }
                if (m_installing)
                {
                    m_resultFailed = true;
                    m_installing = false;
                    m_catalog->invalidateNative();
                    m_clock->stop();
                    finalPage();
                }
                else
                {
                    for (auto button :
                         m_loading->findChildren<QPushButton *>(QStringLiteral("loadingRecovery")))
                        button->show();
                    m_root->setCurrentWidget(m_loading);
                    emit workspaceReady();
                    m_loadText->setText(t("DriverPack could not finish reading this library.\n%1",
                                          "تعذّرت قراءة مكتبة درايفر باك.\n%1")
                                            .arg(e));
                    m_loadBar->setRange(0, 100);
                    m_loadBar->setValue(0);
                }
            });
    connect(m_engine, &EngineBridge::toolFinished, this,
            [this](const QString &s) { showMessage(t("Windows operation", "عملية ويندوز"), s); });
    connect(m_engine, &EngineBridge::snapshotSaved, this,
            [this](const QString &s) { showMessage(t("Report saved", "تم حفظ التقرير"), s); });
    connect(m_catalog, &DriverCatalog::changed, this, [this]() { updateCounts(); });
    connect(m_hardware, &QThread::finished, this,
            [this]()
            {
                m_hardwareData = m_hardware->result;
                if (m_root->currentWidget() == m_expert && m_page == 3)
                    expert(3);
            });
    connect(m_setupTimer, &QTimer::timeout, this, [this]() { pollSoftware(); });
    connect(m_clock, &QTimer::timeout, this,
            [this]()
            {
                if (!m_activeItem.isEmpty() && m_installItemMap.contains(m_activeItem))
                    m_installItemMap.value(m_activeItem)
                        ->setText(2, QStringLiteral("%1 s").arg(
                                         (m_elapsed.elapsed() -
                                          m_itemStarted.value(m_activeItem, m_elapsed.elapsed())) /
                                         1000));
                if (m_elapsedText)
                    m_elapsedText->setText(
                        t("Elapsed: %1:%2", "الوقت: %1:%2")
                            .arg(m_elapsed.elapsed() / 60000, 2, 10, QLatin1Char('0'))
                            .arg((m_elapsed.elapsed() / 1000) % 60, 2, 10, QLatin1Char('0')));
            });
    if (!m_capture)
    {
        for (const QString &id : m_settings->value(QStringLiteral("driverpack/pendingReview")).toStringList())
            m_wanted.insert(id);
        for (const QString &id :
             m_settings->value(QStringLiteral("driverpack/pendingSoftwareReview")).toStringList())
            m_programSelection.insert(id);
        m_settings->remove(QStringLiteral("driverpack/pendingReview"));
        m_settings->remove(QStringLiteral("driverpack/pendingSoftwareReview"));
    }
    UiLanguage::setLanguage(m_language);
    UiFont::applyLanguage(m_language);
    loadSoftware();
    rebuild();
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    SetWindowLongW(hwnd, GWL_STYLE,
                   (GetWindowLongW(hwnd, GWL_STYLE) & ~WS_CAPTION) | WS_THICKFRAME | WS_MINIMIZEBOX |
                       WS_MAXIMIZEBOX | WS_SYSMENU);
    SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm)
    {
        typedef HRESULT(WINAPI * SetAttribute)(HWND, DWORD, LPCVOID, DWORD);
        auto set = reinterpret_cast<SetAttribute>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
        const int rounded = 2;
        if (set)
        {
            set(hwnd, 33, &rounded, sizeof(rounded));
            const DWORD noBorder = 0xfffffffe;
            set(hwnd, 34, &noBorder, sizeof(noBorder));
        }
        FreeLibrary(dwm);
    }
#endif
}

bool DriverPackShell::busy() const { return m_engine->isBusy() || m_installing || m_softwareActive; }
#ifdef Q_OS_WIN
bool DriverPackShell::nativeEvent(const QByteArray &, void *message, long *result)
{
    const MSG *event = static_cast<MSG *>(message);
    if (event->message == WM_NCCALCSIZE)
    {
        *result = 0;
        return true;
    }
    // Keep the system resize/minimize behavior, but never let activation or
    // restoration repaint a native frame over our custom client/title bar.
    if (event->message == WM_NCACTIVATE || event->message == WM_NCPAINT)
    {
        *result = event->message == WM_NCACTIVATE ? TRUE : 0;
        return true;
    }
    if (event->message == WM_NCHITTEST && !isMaximized())
    {
        RECT frame = {};
        POINT point = {};
        GetWindowRect(reinterpret_cast<HWND>(winId()), &frame);
        GetCursorPos(&point);
        const bool left = point.x < frame.left + 7, right = point.x >= frame.right - 7,
                   top = point.y < frame.top + 7, bottom = point.y >= frame.bottom - 7;
        if (top && (left || right))
        {
            *result = left ? HTTOPLEFT : HTTOPRIGHT;
            return true;
        }
        if (bottom && (left || right))
        {
            *result = left ? HTBOTTOMLEFT : HTBOTTOMRIGHT;
            return true;
        }
        if (left || right || top || bottom)
        {
            *result = left ? HTLEFT : right ? HTRIGHT : top ? HTTOP : HTBOTTOM;
            return true;
        }
    }
    if (event->message == WM_GETMINMAXINFO)
    {
        MONITORINFO monitor = {};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(reinterpret_cast<HWND>(winId()), MONITOR_DEFAULTTONEAREST),
                            &monitor))
        {
            auto info = reinterpret_cast<MINMAXINFO *>(event->lParam);
            info->ptMaxPosition.x = monitor.rcWork.left - monitor.rcMonitor.left;
            info->ptMaxPosition.y = monitor.rcWork.top - monitor.rcMonitor.top;
            info->ptMaxSize.x = monitor.rcWork.right - monitor.rcWork.left;
            info->ptMaxSize.y = monitor.rcWork.bottom - monitor.rcWork.top;
            RECT client = {};
            GetClientRect(reinterpret_cast<HWND>(winId()), &client);
            const double scale = width() > 0 && client.right > 0 ? double(client.right) / width() : 1.0;
            info->ptMinTrackSize.x = minimumWidth() * scale;
            info->ptMinTrackSize.y = minimumHeight() * scale;
            *result = 0;
            return true;
        }
    }
    if (event->message == WM_SETTINGCHANGE && m_themeMode == QStringLiteral("system"))
        QTimer::singleShot(0, this, [this]() { appearance(); });
    return false;
}
#endif
void DriverPackShell::closeEvent(QCloseEvent *e)
{
    if (busy())
    {
        showMessage(t("An operation is running", "هناك عملية جارية"),
                    t("Finish the active operation or stop the remaining queue before closing.",
                      "أكمل العملية الحالية أو أوقف باقي القائمة قبل الإغلاق."));
        e->ignore();
        return;
    }
    e->accept();
}
void DriverPackShell::appearance()
{
    m_dark = m_themeMode == QStringLiteral("dark");
    if (m_themeMode == QStringLiteral("system"))
    {
#ifdef Q_OS_WIN
        if (QSysInfo::windowsVersion() >= QSysInfo::WV_WINDOWS10)
        {
            QSettings r(
                QStringLiteral(
                    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                QSettings::NativeFormat);
            m_dark = r.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
        }
#endif
    }
    setProperty("darkMode", m_dark);
    const bool custom = !m_settings->value(QStringLiteral("theme/nativePack")).toString().isEmpty();
    setProperty("nativeCustomTheme", custom);
    const QString surface = custom   ? Theme::color("surface", m_dark).name()
                            : m_dark ? QStringLiteral("#182d3b")
                                     : QStringLiteral("#ffffff"),
                  row = custom   ? Theme::color("elevated", m_dark).name()
                        : m_dark ? QStringLiteral("#203b4b")
                                 : QStringLiteral("#f7f6f4"),
                  ink = custom   ? Theme::color("text", m_dark).name()
                        : m_dark ? QStringLiteral("#ecf5fa")
                                 : QStringLiteral("#163e59"),
                  muted = custom   ? Theme::color("muted", m_dark).name()
                          : m_dark ? QStringLiteral("#adc2cf")
                                   : QStringLiteral("#82919b"),
                  line = custom   ? Theme::color("border", m_dark).name()
                         : m_dark ? QStringLiteral("#3a5260")
                                  : QStringLiteral("#d9dbdd"),
                  hover = custom   ? Theme::color("elevated", m_dark).name()
                          : m_dark ? QStringLiteral("#284f61")
                                   : QStringLiteral("#edf7f8");
    QString sheet =
        QStringLiteral(
            "QWidget#driverPackShell,QWidget#drpBody,QWidget#drpStart,QWidget#drpLoading,QWidget#drpFinal "
            "{background:%1;color:%2;}"
            "QWidget {color:%2;font-size:15px;} QLabel {background:transparent;} QLabel#title "
            "{font-size:29px;font-weight:400;} QLabel#startTitle {font-size:38px;font-weight:300;} "
            "QLabel#section {font-size:21px;font-weight:600;} QLabel#caption {color:%3;font-size:13px;} "
            "QLabel#largeCaption {color:%3;font-size:17px;} QLabel#settingTitle {font-size:17px;} "
            "QLabel#heading {font-size:20px;font-weight:600;}"
            "QWidget#drpHeader {background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #075080,stop:1 "
            "#075953);} QWidget#drpHeader QLabel{color:white;} "
            "QPushButton#windowControl{border:0;border-radius:12px;margin:4px;color:white;background:"
            "transparent;font-size:20px;min-width:42px;min-"
            "height:48px;} QPushButton#windowControl:hover{background:rgba(255,255,255,36);}"
            "QWidget#drpSidebar{background:#07517e;} "
            "QPushButton#drpNav{border:0;border-radius:0;text-align:left;padding:22px "
            "18px;color:#bad2e0;background:transparent;font-size:17px;} "
            "QPushButton#drpNav:checked{color:white;background:rgba(255,255,255,18);font-weight:600;} "
            "QPushButton#drpNav:hover{background:rgba(255,255,255,24);color:white;} "
            "QPushButton#drpBottom{border:0;border-top:1px solid "
            "rgba(255,255,255,36);background:transparent;color:#c3d6e1;text-align:left;padding:17px;font-"
            "size:14px;} QPushButton#drpBottom:hover{background:rgba(255,255,255,24);color:white;}"
            "QPushButton#drpPrimary{border:0;border-radius:12px;background:qlineargradient(x1:0,y1:0,x2:1,y2:"
            "0,stop:0 #00849b,stop:1 #00b5a6);color:white;padding:10px 24px;font-size:22px;font-weight:400;} "
            "QPushButton#drpPrimary:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 "
            "#00768d,stop:1 #00a596);} QPushButton#drpPrimary:disabled{background:%7;color:%3;}"
            "QPushButton#loadingRecovery{border:1px solid %4;border-radius:12px;padding:8px "
            "16px;background:%1;color:%2;} "
            "QPushButton#drpAction{border:1px solid %4;border-radius:12px;background:%1;color:%2;padding:7px "
            "16px;} QPushButton#drpAction:hover{border-color:#00a5ac;background:%5;} "
            "QPushButton#drpAction:disabled{color:%3;background:%7;} "
            "QPushButton#link{background:transparent;border:0;color:#21a9c5;text-align:left;padding:0;} "
            "QPushButton#link:hover{color:#00b9b0;}"
            "QWidget#drpStartReview{background:%6;border:1px solid %4;border-radius:22px;} "
            "QFrame#drpRow{background:%1;border:1px solid %4;border-radius:16px;} "
            "QFrame#drpRow:hover{background:%5;border-color:#58b7bc;} "
            "QWidget#drpGroup{background:%5;border:1px solid %4;border-radius:14px;} "
            "QWidget#drpSetting,QWidget#drpCard{background:%1;border:1px solid %4;border-bottom:2px solid "
            "%4;border-radius:18px;} "
            "QScrollArea,QScrollArea>QWidget>QWidget{background:%1;border:0;}"
            "QCheckBox{background:transparent;spacing:10px;} "
            "QCheckBox::indicator{width:19px;height:19px;border:1px solid "
            "%3;border-radius:3px;background:%1;} QCheckBox::indicator:checked{background:#00aaa9;border:1px "
            "solid #00aaa9;image:url(:/sdio/assets/icons/check.svg);} "
            "QCheckBox::indicator:disabled{border-color:%4;background:%7;}"
            "QProgressBar{border:0;border-radius:9px;background:%7;min-height:12px;text-align:center;} "
            "QProgressBar::chunk{border-radius:9px;background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 "
            "#008aab,stop:1 #00baa5);}"
            "QLineEdit,QComboBox{background:%1;color:%2;border:1px solid %4;border-radius:12px;padding:9px;} "
            "QComboBox QAbstractItemView{background:%1;color:%2;selection-background-color:%5;} "
            "QTreeWidget{background:%1;color:%2;border:1px solid "
            "%4;border-radius:16px;padding:10px;alternate-background-color:%6;} "
            "QHeaderView::section{background:#075c71;color:white;border:0;padding:14px;font-weight:600;} "
            "QTreeView::item{padding:8px;border-bottom:1px solid %4;}"
            "QHeaderView::section:first{border-top-left-radius:10px;border-bottom-left-radius:10px;} "
            "QHeaderView::section:last{border-top-right-radius:10px;border-bottom-right-radius:10px;}"
            "QScrollBar:vertical{background:transparent;width:9px;margin:0;} "
            "QScrollBar::handle:vertical{background:%3;min-height:32px;border-radius:12px;} "
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} "
            "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
            "QDialog{background:%1;} QMenu{background:%1;color:%2;border:1px solid "
            "%4;border-radius:14px;padding:5px;} QMenu::item{padding:9px 24px;} "
            "QMenu::item:selected{background:%5;} QSlider::groove:horizontal{height:4px;background:%4;} "
            "QSlider::sub-page:horizontal{background:#00b3a2;} "
            "QSlider::handle:horizontal{background:%1;border:1px solid "
            "%4;width:28px;border-radius:15px;margin:-13px 0;}")
            .arg(surface, ink, muted, line, hover, row,
                 m_dark ? QStringLiteral("#294653") : QStringLiteral("#edf0f1"));
    if (custom)
    {
        sheet.replace(QStringLiteral("#075080"), Theme::color("headerStart").name());
        sheet.replace(QStringLiteral("#075953"), Theme::color("headerEnd").name());
        sheet.replace(QStringLiteral("#07517e"), Theme::color("sidebarStart").name());
        sheet.replace(QStringLiteral("#00849b"), Theme::color("accent").name());
        sheet.replace(QStringLiteral("#00b5a6"), Theme::color("accent").name());
    }
    if (m_arabic)
        sheet += QStringLiteral("QPushButton#drpNav,QPushButton#drpBottom{text-align:right;}");
    sheet += QStringLiteral("QLabel#signature{font-size:17px;font-weight:600;color:#00a99e;padding:12px "
                            "18px;border:1px solid #53b9bd;border-radius:16px;background:%2;} "
                            "QWidget#drpBody{background:%1;} QWidget#drpRow{background:%2;border:1px solid "
                            "%3;border-radius:18px;} QLabel#section{font-weight:600;} "
                            "QComboBox::drop-down{border:0;width:28px;}")
                 .arg(m_dark ? QStringLiteral("#132c3b") : QStringLiteral("#f4f7f8"), row, line);
    sheet = Theme::styleSheet(m_dark) + sheet;
    sheet +=
        QStringLiteral(
            "QToolTip{color:%1;background:%2;border:1px solid %3;border-radius:9px;padding:9px "
            "10px;font-size:12px;} QScrollBar:vertical{width:9px;margin:0;border:0;background:transparent;} "
            "QScrollBar::handle:vertical{background:%3;min-height:32px;border-radius:4px;margin:1px;} "
            "QScrollBar::handle:vertical:hover,QScrollBar::handle:horizontal:hover{background:#12ada4;} "
            "QScrollBar:horizontal{height:8px;background:transparent;border:0;} "
            "QScrollBar::handle:horizontal{background:%3;border-radius:4px;min-width:35px;} "
            "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;} "
            "QWidget#customTitleBar QLabel{color:#e8fafa;} "
            "QLabel#headerSub,QLabel#headerStatus{font-size:12px;color:#b9e1e6;} "
            "QTreeWidget#diagnosticRecordTable{background:transparent;border:0;padding:0;} "
            "QTreeWidget#diagnosticRecordTable::item{min-height:25px;} ")
            .arg(ink, row, line);
    sheet += QStringLiteral("QTreeView::item:hover{background:%1;color:%2;border-radius:8px;} "
                            "QTreeView::item:selected,QTreeView::item:selected:active,QTreeView::item:"
                            "selected:!active{background:%3;color:%2;border:0;border-radius:8px;} "
                            "QTreeView{outline:0;selection-background-color:%3;selection-color:%2;} "
                            "QTreeView::item:focus{outline:0;border:0;}")
                 .arg(m_dark ? QStringLiteral("#294e60") : QStringLiteral("#eff8f8"), ink,
                      m_dark ? QStringLiteral("#175c72") : QStringLiteral("#cdece8"));
    sheet += QStringLiteral("QPlainTextEdit#bugDescription,QPlainTextEdit#bugEnvironment{background:%1;color:"
                            "%2;border:1px solid %3;border-radius:12px;padding:12px;}")
                 .arg(row, ink, line);
    sheet += QStringLiteral("QTreeWidget[projectRecords=\"true\"]::item{background:transparent;border:0;"
                            "border-radius:0;padding:10px 12px;} "
                            "QTreeWidget[projectRecords=\"true\"]::item:selected,QTreeWidget[projectRecords="
                            "\"true\"]::item:hover{background:transparent;border:0;border-radius:0;}");
    sheet += QStringLiteral("QFrame#driverChoiceRow{background:transparent;border:0;border-radius:12px;} "
                            "QFrame#driverChoiceRow:hover{background:%1;} "
                            "QFrame#driverChoiceRow[selectedDriver=\"true\"]{background:%2;} "
                            "QPushButton#rowIconAction{background:transparent;border:0;border-radius:9px;"
                            "padding:0;font-size:23px;} QPushButton#rowIconAction:hover{background:%1;}")
                 .arg(m_dark ? QStringLiteral("#294e60") : QStringLiteral("#edf7f8"),
                      m_dark ? QStringLiteral("#175c72") : QStringLiteral("#dcefeb"));
    sheet += QStringLiteral(
        "QTabWidget::pane{border:0;background:transparent;top:0;} QTabBar{background:transparent;border:0;} "
        "QTabBar::tab{padding:0;margin:0;border:0;background:transparent;}");
    sheet +=
        QStringLiteral("QScrollArea#driverDetailScroll,QWidget#driverDetailViewport,QWidget#"
                       "driverDetailCanvas,QWidget#addProgramCanvas,QScrollArea#addProgramScroll{background:"
                       "transparent;border:0;} QWidget#settingsPage,QWidget#diagnosticsPage{background:%1;} "
                       "QTabWidget#settingsTabs{background:transparent;border:0;} "
                       "QTabWidget#settingsTabs::pane{background:transparent;border:0;top:0;} "
                       "QScrollBar:vertical{width:12px;} QScrollBar:horizontal{height:12px;}")
            .arg(Theme::color("background", m_dark).name());
    sheet += QStringLiteral("QListWidget#diagnosticNavigation{background:transparent;border:0;padding:4px;} "
                            "QListWidget#diagnosticNavigation::item{background:%1;border:1px solid "
                            "%2;border-radius:12px;padding:9px;color:%3;} "
                            "QListWidget#diagnosticNavigation::item:selected{background:%4;color:%3;border-"
                            "color:#27aaa5;} QListWidget#diagnosticNavigation::item:hover{background:%4;}")
                 .arg(Theme::color("surface", m_dark).name(), line, ink,
                      m_dark ? QStringLiteral("#225261") : QStringLiteral("#def1ee"));
    sheet += QStringLiteral("QPushButton[projectAction=\"true\"]{border:1px solid "
                            "%1;border-radius:12px;background:%2;color:%3;padding:7px 16px;} "
                            "QPushButton[projectAction=\"true\"]:hover{background:%4;border-color:#00aaa9;} "
                            "QPushButton[projectAction=\"true\"]:disabled{color:%5;}")
                 .arg(line, Theme::color("surface", m_dark).name(), ink,
                      m_dark ? QStringLiteral("#294e60") : QStringLiteral("#eaf6f4"), muted);
    setStyleSheet(sheet);
    refreshChrome();
}
void DriverPackShell::rebuild()
{
    if (m_tools)
    {
        m_tools->hide();
        m_tools->deleteLater();
        m_tools = 0;
    }
    if (layout())
    {
        QLayoutItem *item;
        while ((item = layout()->takeAt(0)))
        {
            if (item->widget())
            {
                item->widget()->hide();
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete layout();
    }
    m_primary = m_footerPrimary = m_softwarePrimary = 0;
    m_elapsedText = m_installText = 0;
    m_installBar = 0;
    m_installItems = 0;
    m_nav.clear();
    setLayoutDirection(UiLanguage::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);
    setWindowTitle(t("DriverPack Modern", "درايفر باك مودرن"));
    auto all = new QVBoxLayout(this);
    all->setContentsMargins(0, 0, 0, 0);
    all->setSpacing(0);
    auto titlebar = new TitleBar(this);
    m_header = titlebar;
    m_headerLayout = 0;
    m_brand = 0;
    titlebar->setArabic(m_arabic);
    titlebar->setDark(m_dark);
    connect(titlebar, &TitleBar::themeRequested, this,
            [this](bool dark) { setDark(dark ? QStringLiteral("dark") : QStringLiteral("light")); });
    connect(titlebar, &TitleBar::sidebarRequested, this, [this]() { toggleSidebar(); });
    connect(titlebar, &TitleBar::menuRequested, this, [this]() { tools(); });
    connect(titlebar, &TitleBar::minimizeRequested, this, [this]() { showMinimized(); });
    connect(titlebar, &TitleBar::maximizeRequested, this,
            [this, titlebar]()
            {
                isMaximized() ? showNormal() : showMaximized();
                titlebar->setMaximized(isMaximized());
            });
    connect(titlebar, &TitleBar::closeRequested, this, [this]() { close(); });
    all->addWidget(m_header);
    m_header->setVisible(!m_initial);
    m_root = new QStackedWidget(this);
    all->addWidget(m_root, 1);
    m_loading = new QWidget;
    m_loading->setObjectName(QStringLiteral("drpLoading"));
    auto loading = new QVBoxLayout(m_loading);
    loading->setContentsMargins(24, 24, 24, 24);
    loading->addStretch();
    auto loadCard = new AccentCard(m_loading);
    loadCard->setStyleSheet(QStringLiteral("QLabel{color:#e9fbfc;}"));
    loadCard->setObjectName(QStringLiteral("drpCard"));
    loadCard->setMaximumWidth(580);
    auto cardBox = new QVBoxLayout(loadCard);
    cardBox->setContentsMargins(32, 32, 32, 28);
    cardBox->setSpacing(20);
    auto loadingLogo = copy(QStringLiteral("DriverPack Modern"), loadCard, "title");
    loadingLogo->setAlignment(Qt::AlignCenter);
    cardBox->addWidget(loadingLogo);
    m_loadText =
        copy(t("Reading your computer and the DriverPack library…", "جارٍ قراءة جهازك ومكتبة درايفر باك…"),
             loadCard, "largeCaption");
    m_loadText->setAlignment(Qt::AlignCenter);
    cardBox->addWidget(m_loadText);
    m_loadBar = new QProgressBar(loadCard);
    m_loadBar->setRange(0, 0);
    m_loadBar->setMinimumWidth(420);
    m_loadBar->setMaximumWidth(480);
    cardBox->addWidget(m_loadBar, 0, Qt::AlignHCenter);
    auto retry = action(t("Scan again", "إعادة الفحص"), loadCard);
    retry->setObjectName(QStringLiteral("loadingRecovery"));
    retry->hide();
    connect(retry, &QPushButton::clicked, this, [this]() { beginScan(); });
    cardBox->addWidget(retry, 0, Qt::AlignHCenter);
    auto browse = action(t("Choose DriverPack library…", "اختيار مكتبة درايفر باك…"), loadCard);
    browse->setObjectName(QStringLiteral("loadingRecovery"));
    browse->hide();
    connect(browse, &QPushButton::clicked, this, [this]() { chooseLibrary(); });
    cardBox->addWidget(browse, 0, Qt::AlignHCenter);

    auto credit = copy(t("Designed by Tarek Sadek", "تصميم وتطوير طارق صادق"), loadCard, "signature");
    credit->setObjectName(QStringLiteral("loadingCredit"));
    credit->setStyleSheet(
        QStringLiteral("font-size:13px;color:#d5f4f4;background:transparent;border:0;padding:8px;"));
    credit->setMinimumWidth(320);
    credit->setAlignment(Qt::AlignCenter);
    credit->setWordWrap(false);
    cardBox->addWidget(credit, 0, Qt::AlignHCenter);
    loading->addWidget(loadCard, 0, Qt::AlignHCenter);
    loading->addStretch();
    auto splashShadow = new QGraphicsDropShadowEffect(loadCard);
    splashShadow->setBlurRadius(20);
    splashShadow->setOffset(0, 4);
    splashShadow->setColor(QColor(0, 0, 0, 35));
    loadCard->setGraphicsEffect(splashShadow);
    m_root->addWidget(m_loading);
    m_start = new QWidget;
    m_start->setObjectName(QStringLiteral("drpStart"));
    m_root->addWidget(m_start);
    m_expert = new QWidget;
    auto expertLayout = new QHBoxLayout(m_expert);
    expertLayout->setContentsMargins(0, 0, 0, 0);
    expertLayout->setSpacing(0);
    auto side = new Sidebar(m_expert);
    m_side = side;
    side->setArabic(m_arabic);
    side->setAnimations(false);
    if (m_settings->value(QStringLiteral("driverpack/compactMenu"), false).toBool())
        side->toggleCollapsed();
    side->setAnimations(true);
    for (auto button : side->findChildren<SidebarItem *>())
        m_nav.insert(button->property("pageIndex").toInt(), button);
    if (m_nav.contains(2))
        m_nav[2]->setVisible(m_settings->value(QStringLiteral("driverpack/showProtection"), true).toBool());
    if (m_nav.contains(3))
        m_nav[3]->setVisible(
            m_settings->value(QStringLiteral("driverpack/enableDiagnostics"), true).toBool());
    connect(side, &Sidebar::pageRequested, this, [this](int page) { expert(page); });
    expertLayout->addWidget(m_side);
    m_pages = new QStackedWidget(m_expert);
    for (int i = 0; i < 7; ++i)
        m_pages->addWidget(new QWidget);
    expertLayout->addWidget(m_pages, 1);
    m_root->addWidget(m_expert);
    m_install = new QWidget;
    m_final = new QWidget;
    m_final->setObjectName(QStringLiteral("drpFinal"));
    m_root->addWidget(m_install);
    m_root->addWidget(m_final);
    connect(m_root, &QStackedWidget::currentChanged, this, [this](int) { refreshChrome(); });
    m_tools = new ToolsDrawer(this);
    m_tools->setLayoutDirection(UiLanguage::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);
    m_tools->setArabic(m_arabic);
    m_tools->setMotionEnabled(true);
    m_tools->setGeometry(0, m_header->height(), width(), height() - m_header->height());
    const QString unsupported =
        t("This tool needs SDIO indexes or snapshots. DriverPack uses its own SQLite library.",
          "هذه الأداة تحتاج فهارس أو لقطات SDIO. يستخدم درايفر باك مكتبة SQLite الخاصة به.");
    for (const QString &id :
         {QStringLiteral("load-snapshot"), QStringLiteral("reindex"), QStringLiteral("index-report")})
        m_tools->setActionAvailable(id, false, unsupported);
    connect(m_tools, &ToolsDrawer::actionRequested, this, [this](const QString &id) { toolAction(id); });
    appearance();
    if (!m_initial)
        startPage();
    if (!m_initial)
    {
        if (m_settings->value(QStringLiteral("driverpack/startExpert"), false).toBool())
            expert(m_page);
        else
            m_root->setCurrentWidget(m_start);
    }
    else
        m_root->setCurrentWidget(m_loading);
}
void DriverPackShell::loadSoftware()
{
    try
    {
        m_programs = DriverPackSoftware::load(m_library);
        if(m_demo)for(auto &program:m_programs){program.installed=false;program.localPath.clear();}
        QSet<QString> eligible;
        for (const auto &p : m_programs)
            if (!p.excluded && !p.installed)
                eligible.insert(p.id);
        m_programSelection.intersect(eligible);
    }
    catch (const std::exception &e)
    {
        m_programs.clear();
        m_error = QString::fromUtf8(e.what());
    }
}
void DriverPackShell::applyOptions()
{
    sdiomodern::Paths p;
    p.drivers = QDir(m_library).filePath(QStringLiteral("drivers")).toStdWString();
    p.indexes = QDir(m_library).filePath(QStringLiteral("index")).toStdWString();
    p.logDirectory = QDir(m_library).filePath(QStringLiteral("logs")).toStdWString();
    p.noLog = !m_settings->value(QStringLiteral("driverpack/enableLogging"), true).toBool();
    p.keepTempFiles = !m_settings->value(QStringLiteral("driverpack/removeTemp"), false).toBool();
    m_engine->setSessionOptions(p);
}
void DriverPackShell::beginScan()
{
    if (busy())
        return;
    m_error.clear();
    if (!QFileInfo(QDir(m_library).filePath(QStringLiteral("index/db.sqlite"))).isFile())
    {
        for (auto button : m_loading->findChildren<QPushButton *>(QStringLiteral("loadingRecovery")))
            button->show();
        m_root->setCurrentWidget(m_loading);
        emit workspaceReady();
        m_header->show();
        m_loadBar->setRange(0, 100);
        m_loadText->setText(t("Choose a DriverPack offline library containing index/db.sqlite and "
                              "drivers.\nNo library is bundled inside this executable.",
                              "اختر مكتبة درايفر باك أوفلاين التي تحتوي index/db.sqlite ومجلد drivers.\nملف "
                              "التشغيل لا يحتوي حزم التعريفات الكبيرة."));
        return;
    }
    m_catalog->invalidateNative();
    applyOptions();
    for (auto button : m_loading->findChildren<QPushButton *>(QStringLiteral("loadingRecovery")))
        button->hide();
    m_loadBar->setRange(0, 0);
    if (!m_initial)
    {
        expert(0);
        new ContentLoader(m_pages, t("Matching driver packages", "مطابقة حزم التعريفات"),
                          t("Please wait while the offline library is matched to this computer.",
                            "يرجى الانتظار أثناء مطابقة المكتبة الأوفلاين مع جهازك."));
    }
    else
        m_root->setCurrentWidget(m_loading);
    m_engine->requestInventory(m_demo);
}
void DriverPackShell::snapshot(const sdiomodern::Snapshot &s)
{
    for (auto overlay :
         m_pages->findChildren<QWidget *>(QStringLiteral("contentLoader"), Qt::FindDirectChildrenOnly))
    {
        overlay->hide();
        overlay->deleteLater();
    }
    m_remote.clear();
    m_remoteChoices.clear();
    m_catalog->replaceNative(s);
    QVector<DeviceInfo> devices;
    for (const auto &e : m_catalog->entries())
        devices << e.device;
    const auto profile = SystemProfile::collect(devices, false);
    m_model = profile.value(QStringLiteral("model"));
    if (m_model.isEmpty())
        m_model = profile.value(QStringLiteral("computer"));
    loadSoftware();
    if (m_settings->value(QStringLiteral("driverpack/defaultRecommendations"), false).toBool())
        selectRecommended();
    for (int i = 0; i < m_catalog->size(); ++i)
        if (m_wanted.contains(m_catalog->entryRef(i).device.instanceId))
            m_catalog->setSelected(i, true);
    m_wanted.clear();
    if (m_catalog->selectedDrivers() || !m_programSelection.isEmpty())
        m_mode = 1;
    const bool first = m_initial;
    if (first)
    {
        setMinimumSize(900, 650);
        const QRect usable = QApplication::desktop()->availableGeometry(this);
        resize(qMin(1360, usable.width() - 24), qMin(850, usable.height() - 24));
        move(usable.center() - rect().center());
        m_header->show();
    }
    m_initial = false;
    startPage();
    if (first && !m_settings->value(QStringLiteral("driverpack/startExpert"), false).toBool())
        m_root->setCurrentWidget(m_start);
    else
        expert(m_page);
    if (first)
        emit workspaceReady();
}
void DriverPackShell::startPage()
{
    if (m_start->layout())
    {
        qDeleteAll(m_start->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
        delete m_start->layout();
    }
    auto whole = new QVBoxLayout(m_start);
    whole->setContentsMargins(28, 24, 28, 26);
    auto card = new WelcomeCard(m_start);
    card->setObjectName(QStringLiteral("welcomeCard"));
    whole->addWidget(card, 1);
    auto outer = new QVBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto banner = new QWidget(card);
    banner->setFixedHeight(218);
    auto hero = new QVBoxLayout(banner);
    hero->setContentsMargins(34, 22, 34, 22);
    hero->setSpacing(8);
    auto identity = new QHBoxLayout;
    identity->addStretch();
    auto emblem = new QLabel(banner);
    emblem->setPixmap(QIcon(QStringLiteral(":/sdio/assets/driverpack-emblem.png")).pixmap(32, 32));
    identity->addWidget(emblem);
    auto brand = copy(QStringLiteral("DRIVERPACK MODERN"), banner);
    brand->setStyleSheet(QStringLiteral("color:#ccf5f0;font-size:13px;font-weight:600;letter-spacing:2px;"));
    identity->addWidget(brand);
    identity->addStretch();
    hero->addLayout(identity);
    auto title = copy(t("Automatic configuring of", "الإعداد التلقائي لجهاز"), banner);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("color:#ceebee;font-size:19px;"));
    hero->addWidget(title);
    auto device = copy(m_model.isEmpty() ? t("This computer", "هذا الكمبيوتر") : m_model, banner);
    device->setAlignment(Qt::AlignCenter);
    device->setStyleSheet(QStringLiteral("color:white;font-size:29px;font-weight:600;"));
    hero->addWidget(device);
    auto subtitle = copy(t("A clearer view of your drivers. You choose what happens next.",
                           "صورة أوضح لتعريفات جهازك، والقرار لك في الخطوة التالية."),
                         banner);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color:#c9e8e8;font-size:13px;"));
    hero->addWidget(subtitle);
    outer->addWidget(banner);
    auto body = new QWidget(card);
    auto content = new QVBoxLayout(body);
    content->setContentsMargins(36, 24, 36, 20);
    content->setSpacing(14);
    outer->addWidget(body, 1);
    content->addStretch(1);
    auto install = action(m_mode == 0 ? t("PC diagnostics and configuring", "تشخيص الكمبيوتر وإعداده")
                                      : t("Review selected items", "مراجعة العناصر المحددة"),
                          body, true);
    install->setMinimumSize(470, 62);
    install->setMaximumWidth(680);
    install->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    install->setIcon(VectorIcons::icon(QStringLiteral("spark"), Qt::white, 28));
    install->setIconSize(QSize(28, 28));
    auto primaryRow = new QHBoxLayout;
    primaryRow->addStretch(1);
    primaryRow->addWidget(install, 5);
    primaryRow->addStretch(1);
    content->addLayout(primaryRow);
    connect(install, &QPushButton::clicked, this,
            [this]()
            {
                if (m_mode == 0 && m_programSelection.isEmpty())
                {
                    expert(3);
                    return;
                }
                if (m_mode == 2 && !m_catalog->selectedDrivers())
                    selectRecommended();
                startInstall();
            });
    auto note =
        copy(m_mode == 0 ? t("Diagnostics only. Drivers and programs are installed after your confirmation.",
                             "فحص فقط. يتم تثبيت التعريفات والبرامج بعد تأكيدك.")
                         : t("Your selected items will be reviewed before installation.",
                             "ستتم مراجعة العناصر التي اخترتها قبل التثبيت."),
             body, "caption");
    note->setAlignment(Qt::AlignCenter);
    content->addWidget(note);
    auto expertButton = action(t("Expert mode", "وضع الخبير"), body);
    expertButton->setFixedSize(200, 44);
    expertButton->setObjectName(QStringLiteral("drpPrimary"));
    expertButton->setProperty("projectAction", false);
    expertButton->setStyleSheet(
        QStringLiteral("QPushButton#drpPrimary{font-size:16px;font-weight:600;padding:0 12px;}"));
    expertButton->setIcon(VectorIcons::icon(QStringLiteral("sliders"), Qt::white, 21));
    expertButton->setIconSize(QSize(21, 21));
    connect(expertButton, &QPushButton::clicked, this, [this]() { expert(0); });
    content->addWidget(expertButton, 0, Qt::AlignHCenter);
    content->addStretch(1);
    auto included = copy(t("ONE CHECK. A COMPLETE PICTURE.", "فحص واحد، صورة متكاملة."), body);
    included->setAlignment(Qt::AlignCenter);
    included->setStyleSheet(
        QStringLiteral("font-size:12px;font-weight:600;color:%1;").arg(Theme::color("muted", m_dark).name()));
    content->addWidget(included);
    auto features = new QHBoxLayout;
    features->setSpacing(12);
    const char *en[] = {"Network", "Display & sound", "Windows devices", "System information"};
    const char *ar[] = {"الشبكة", "العرض والصوت", "أجهزة ويندوز", "معلومات النظام"};
    const char *icons[] = {"network", "audio", "chip", "computer"};
    for (int i = 0; i < 4; ++i)
    {
        auto feature = new QWidget(body);
        auto box = new QVBoxLayout(feature);
        box->setContentsMargins(14, 14, 14, 14);
        box->setSpacing(8);
        auto icon = new QLabel(feature);
        icon->setPixmap(
            VectorIcons::icon(QString::fromLatin1(icons[i]), QColor("#00aaa9"), 24).pixmap(24, 24));
        icon->setAlignment(Qt::AlignCenter);
        box->addWidget(icon);
        auto text = copy(t(en[i], ar[i]), feature);
        text->setAlignment(Qt::AlignCenter);
        text->setStyleSheet(QStringLiteral("font-size:13px;font-weight:500;"));
        box->addWidget(text);
        feature->setStyleSheet(
            QStringLiteral(
                "QWidget{background:%1;border-radius:16px;} QLabel{background:transparent;border:0;}")
                .arg(Theme::color("elevated", m_dark).name()));
        features->addWidget(feature, 1);
    }
    content->addLayout(features);
    const int drivers = m_catalog->selectedDrivers();
    if (drivers || !m_programSelection.isEmpty())
    {
        auto selected = copy(t("Selected: %1 drivers · %2 programs", "المحدد: %1 تعريف · %2 برنامج")
                                 .arg(drivers)
                                 .arg(m_programSelection.size()),
                             body, "caption");
        selected->setAlignment(Qt::AlignCenter);
        content->addWidget(selected);
    }
    content->addStretch(1);
    auto designer = new QWidget(body);
    designer->setObjectName(QStringLiteral("designerCapsule"));
    designer->setFixedHeight(48);
    designer->setStyleSheet(
        QStringLiteral(
            "QWidget#designerCapsule{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %1,stop:1 "
            "%2);border:1px solid %3;border-radius:23px;} QLabel{background:transparent;border:0;padding:0;}")
            .arg(m_dark ? QStringLiteral("#173e50") : QStringLiteral("#edf8fa"),
                 m_dark ? QStringLiteral("#17463f") : QStringLiteral("#eaf6f1"),
                 m_dark ? QStringLiteral("#346b70") : QStringLiteral("#c3e3df")));
    auto signature = new QHBoxLayout(designer);
    signature->setContentsMargins(18, 5, 18, 5);
    signature->setSpacing(7);
    auto made = copy(t("Made with", "صُنع بكل"), designer);
    made->setStyleSheet(QStringLiteral("font-size:13px;color:%1;").arg(Theme::color("muted", m_dark).name()));
    signature->addWidget(made);
    signature->addWidget(createSignatureHeart(designer));
    auto by = copy(t("by", "بواسطة"), designer);
    by->setStyleSheet(made->styleSheet());
    signature->addWidget(by);
    auto name = copy(t("Tarek Sadek · Diagoo1", "طارق صادق · Diagoo1"), designer);
    name->setStyleSheet(
        QStringLiteral("font-size:14px;font-weight:600;color:%1;").arg(Theme::color("link", m_dark).name()));
    signature->addWidget(name);
    content->addWidget(designer, 0, Qt::AlignHCenter);
}
void DriverPackShell::expert(int page)
{
    if (m_installing || m_softwareActive)
        return;
    m_page = qBound(0, page, 6);
    m_primary = m_footerPrimary = m_softwarePrimary = 0;
    QWidget *old = m_pages->widget(m_page);
    int scroll = 0;
    if (auto area = old->findChild<QScrollArea *>())
        scroll = area->verticalScrollBar()->value();
    m_pages->removeWidget(old);
    old->hide();
    old->deleteLater();
    auto body = new QWidget;
    m_pages->insertWidget(m_page, body);
    switch (m_page)
    {
    case 0:
        buildDrivers(body);
        break;
    case 1:
        buildSoftware(body);
        break;
    case 2:
        buildProtection(body);
        break;
    case 3:
        buildDiagnostics(body);
        break;
    case 4:
        buildOffline(body);
        break;
    case 5:
        buildSettings(body);
        break;
    default:
        buildAbout(body);
        break;
    }
    for (auto button : body->findChildren<QAbstractButton *>())
        if (button->toolTip().isEmpty() && !button->text().isEmpty())
            button->setToolTip(button->text());
    m_pages->setCurrentIndex(m_page);
    if (auto side = qobject_cast<Sidebar *>(m_side))
        side->setCurrentPage(m_page);
    m_root->setCurrentWidget(m_expert);
    if (auto area = body->findChild<QScrollArea *>())
        QTimer::singleShot(0, area, [area, scroll]() { area->verticalScrollBar()->setValue(scroll); });
    updateCounts();
}
void DriverPackShell::toggleSidebar()
{
    if (auto side = qobject_cast<Sidebar *>(m_side))
    {
        m_settings->setValue(QStringLiteral("driverpack/compactMenu"),
                             !m_settings->value(QStringLiteral("driverpack/compactMenu"), false).toBool());
        side->toggleCollapsed();
    }
}
void DriverPackShell::updateCounts()
{
    if (m_batchSelecting)
        return;
    const int count = m_catalog->selectedDrivers() + m_remote.size() + m_programSelection.size();
    if (m_mode == 1 && count == 0)
        m_mode = 0;
    else if (m_mode == 0 && count > 0)
        m_mode = 1;
    if (m_pages)
        for (auto tick : m_pages->findChildren<QAbstractButton *>())
        {
            if (!tick->property("driverRowIndex").isValid())
                continue;
            const auto entry = m_catalog->entry(tick->property("driverRowIndex").toInt());
            const int candidate = tick->property("driverCandidateIndex").toInt();
            const bool local = entry.checked && (candidate < 0 || entry.chosenCandidate == candidate);
            const bool remote =
                m_remote.contains(entry.device.instanceId) &&
                (candidate < 0 || m_remoteChoices.value(entry.device.instanceId, -1) == candidate);
            const QSignalBlocker blocker(tick);
            tick->setChecked(local || remote);
            auto face = tick->parentWidget();
            if (face && face->objectName() == QStringLiteral("driverChoiceRow") &&
                face->property("selectedDriver").toBool() != (local || remote))
            {
                face->setProperty("selectedDriver", local || remote);
                face->style()->unpolish(face);
                face->style()->polish(face);
                face->update();
            }
        }
    if (m_pages)
        for (auto group : m_pages->findChildren<QCheckBox *>(QStringLiteral("driverGroupToggle")))
        {
            int available = 0, selected = 0;
            for (const auto &value : group->property("groupRows").toList())
            {
                const int row = value.toInt();
                if (groupCandidate(m_catalog, row) < 0)
                    continue;
                ++available;
                const auto &entry = m_catalog->entryRef(row);
                if (entry.checked || m_remote.contains(entry.device.instanceId))
                    ++selected;
            }
            const QSignalBlocker blocker(group);
            group->setCheckState(selected == 0           ? Qt::Unchecked
                                 : selected == available ? Qt::Checked
                                                         : Qt::PartiallyChecked);
            group->setEnabled(available > 0 && !busy());
            group->setToolTip(
                selected == available && available > 0
                    ? t("Clear this group", "إلغاء تحديد المجموعة")
                    : t("Select available drivers in this group", "تحديد التعريفات المتاحة في المجموعة"));
        }
    if (m_pages)
        for (auto tick : m_pages->findChildren<QCheckBox *>())
        {
            const QString id = tick->property("softwareId").toString();
            if (!id.isEmpty())
            {
                const QSignalBlocker blocker(tick);
                tick->setChecked(m_programSelection.contains(id));
            }
            if (tick->objectName() == QStringLiteral("softwareGroupToggle"))
            {
                const auto ids = tick->property("softwareIds").toStringList();
                int selected = 0;
                for (const auto &key : ids)
                    selected += m_programSelection.contains(key);
                const QSignalBlocker blocker(tick);
                tick->setCheckState(!selected                ? Qt::Unchecked
                                    : selected == ids.size() ? Qt::Checked
                                                             : Qt::PartiallyChecked);
                tick->setEnabled(!ids.isEmpty() && !busy());
            }
        }
    for (auto b : {m_primary, m_footerPrimary})
        if (b)
        {
            b->setText(count > 0 ? t("Install all  (%1)", "تثبيت الكل  (%1)").arg(count)
                                 : t("Scan this computer", "فحص هذا الكمبيوتر"));
            b->setIcon(VectorIcons::icon(count > 0 ? QStringLiteral("install") : QStringLiteral("scan"),
                                         Qt::white, 28));
            b->setToolTip(count > 0 ? t("Review selected drivers and software before installation",
                                        "مراجعة التعريفات والبرامج المحددة قبل التثبيت")
                                    : t("Scan the computer for matching drivers",
                                        "فحص الكمبيوتر والبحث عن التعريفات المطابقة"));
            b->setEnabled(!busy());
        }
    if (m_softwarePrimary)
    {
        m_softwarePrimary->setText(t("Install the software programs  (%1)", "تثبيت البرامج المحددة  (%1)")
                                       .arg(m_programSelection.size()));
        m_softwarePrimary->setEnabled(!busy());
        m_softwarePrimary->setToolTip(m_programSelection.isEmpty()
                                          ? t("Select programs first", "حدد البرامج أولًا")
                                          : t("Review selected programs", "مراجعة البرامج المحددة"));
    }
}
void DriverPackShell::selectRecommended()
{
    m_remote.clear();
    m_remoteChoices.clear();
    m_catalog->selectAll(true);
    for (int i = 0; i < m_catalog->size(); ++i)
    {
        const auto e = m_catalog->entry(i);
        for (int j = 1; j < e.candidates.size(); ++j)
            if (e.candidates[j].valid && e.candidates[j].recommended && !e.candidates[j].local &&
                (e.candidates[j].statusBits & (0x10u | 0x20u)))
            {
                m_remoteChoices[e.device.instanceId] = j;
                setDriver(i, true);
                break;
            }
    }
    updateCounts();
}
void DriverPackShell::setDriver(int row, bool checked)
{
    const auto e = m_catalog->entry(row);
    if (checked && m_mode == 0)
        m_mode = 1;
    if (!checked)
    {
        m_remote.remove(e.device.instanceId);
        m_catalog->setSelected(row, false);
        updateCounts();
        return;
    }
    const int chosen =
        groupCandidate(m_catalog, row, m_remoteChoices.value(e.device.instanceId, e.chosenCandidate));
    if (chosen > 0 && chosen < e.candidates.size() && e.candidates[chosen].valid)
    {
        const bool downgrade = (e.candidates[chosen].statusBits & 0x80u) != 0;
        const bool reinstall = isReinstallCandidate(e, chosen);
        if (!m_capture && !m_batchSelecting && (downgrade || reinstall) &&
            (!e.checked || chosen != e.chosenCandidate))
        {
            const QString title =
                downgrade ? t("Downgrade", "الرجوع لإصدار أقدم") : t("Reinstall", "إعادة تثبيت");
            const QString message =
                downgrade
                    ? t("This replaces the installed driver with an older version.\n\n%1\n%2 → %3\n\nSelect "
                        "it for manual installation?",
                        "سيستبدل هذا الاختيار التعريف المثبت بإصدار أقدم.\n\n%1\n%2 ← %3\n\nهل تريد تحديده "
                        "للتثبيت اليدوي؟")
                          .arg(e.device.name, e.device.driverVersion, e.candidates[chosen].version)
                    : t("The selected package matches your installed driver. Select it for "
                        "reinstallation?\n\n%1 · %2",
                        "الحزمة المختارة تطابق التعريف المثبت. هل تريد تحديدها لإعادة التثبيت؟\n\n%1 · %2")
                          .arg(e.device.name, e.candidates[chosen].version);
            if (!confirm(title, message))
            {
                m_remoteChoices.remove(e.device.instanceId);
                m_remote.remove(e.device.instanceId);
                updateCounts();
                return;
            }
        }
        if (!e.candidates[chosen].local)
        {
            m_remoteChoices[e.device.instanceId] = chosen;
            m_remote.insert(e.device.instanceId);
            m_catalog->setSelected(row, false);
        }
        else
        {
            m_remote.remove(e.device.instanceId);
            m_catalog->chooseCandidate(row, chosen);
            m_catalog->setSelected(row, true);
        }
    }
    updateCounts();
}
void DriverPackShell::section(QVBoxLayout *box, const QString &title, const QVector<int> &rows)
{
    auto group = new QWidget;
    group->setObjectName(QStringLiteral("drpGroup"));
    auto bar = new QHBoxLayout(group);
    bar->setContentsMargins(18, 18, 18, 18);
    bar->addWidget(copy(title, group, "section"), 1);
    if (!rows.isEmpty())
    {
        auto all = new GroupCheck(group);
        all->setObjectName(QStringLiteral("driverGroupToggle"));
        QVariantList ids;
        for (int row : rows)
            ids << row;
        all->setProperty("groupRows", ids);
        all->setAccessibleName(t("Select or clear this group", "تحديد المجموعة أو إلغاء تحديدها"));
        connect(
            all, &QCheckBox::clicked, this,
            [this, rows, all]()
            {
                const bool select = all->checkState() == Qt::Checked;
                QStringList older;
                if (select && !m_capture)
                    for (int row : rows)
                    {
                        const int candidate = groupCandidate(m_catalog, row);
                        if (candidate > 0 &&
                            (m_catalog->entryRef(row).candidates[candidate].statusBits & 0x80u))
                            older << m_catalog->entryRef(row).device.name;
                    }
                if (!older.isEmpty() &&
                    !confirm(t("Downgrade", "الرجوع لإصدار أقدم"),
                             t("This group includes older driver versions. Select them manually?\n\n%1",
                               "تحتوي المجموعة إصدارات تعريفات أقدم من المثبتة. هل تريد تحديدها يدويًا؟\n\n%1")
                                 .arg(older.join(QLatin1Char('\n')))))
                {
                    updateCounts();
                    return;
                }
                {
                    const QSignalBlocker signalGuard(m_catalog);
                    QScopedValueRollback<bool> batch(m_batchSelecting, true);
                    for (int row : rows)
                        if (!select || groupCandidate(m_catalog, row) >= 0)
                            setDriver(row, select);
                }
                updateCounts();
            });
        bar->addWidget(all);
    }
    box->addWidget(group);
}
void DriverPackShell::buildDrivers(QWidget *page)
{
    QVBoxLayout *box;
    auto body = pageBody(page, &box);
    // Keep the card width stable when an expansion first needs scrolling.
    page->findChild<QScrollArea *>()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    box->setSpacing(12);
    box->addWidget(copy(t("Drivers for this computer", "تعريفات هذا الكمبيوتر"), body, "title"));
    box->addSpacing(38);
    auto header = new QHBoxLayout;
    m_primary = action(QString(), body, true);
    m_primary->setMinimumWidth(370);
    m_primary->setIcon(VectorIcons::icon(QStringLiteral("download"), Qt::white, 32));
    m_primary->setIconSize(QSize(32, 32));
    header->addWidget(m_primary, 1);
    connect(m_primary, &QPushButton::clicked, this, [this]() { startInstall(); });
    auto options = new QVBoxLayout;
    auto installed = new DrpSwitch(
        t("View the installed and alternative drivers", "عرض التعريفات المثبتة والبديلة"), body);
    installed->setObjectName(QStringLiteral("showInstalledToggle"));
    installed->setChecked(m_showInstalled);
    auto installedRefresh = new QTimer(installed);
    installedRefresh->setSingleShot(true);
    connect(installedRefresh, &QTimer::timeout, this,
            [this]()
            {
                if (m_page == 0 && m_root->currentWidget() == m_expert)
                    expert(0);
            });
    connect(installed, &QCheckBox::toggled, this,
            [this, installedRefresh](bool value)
            {
                m_showInstalled = value;
                installedRefresh->start(180);
            });
    auto additional = new DrpSwitch(t("View additional information", "عرض معلومات إضافية"), body);
    additional->setObjectName(QStringLiteral("showAdditionalToggle"));
    additional->setChecked(m_additional);
    auto additionalRefresh = new QTimer(additional);
    additionalRefresh->setSingleShot(true);
    connect(additionalRefresh, &QTimer::timeout, this,
            [this]()
            {
                if (m_page == 0 && m_root->currentWidget() == m_expert)
                    expert(0);
            });
    connect(additional, &QCheckBox::toggled, this,
            [this, additionalRefresh](bool value)
            {
                m_additional = value;
                additionalRefresh->start(180);
            });
    options->addWidget(installed);
    options->addWidget(additional);
    header->addSpacing(50);
    header->addLayout(options, 1);
    box->addLayout(header);
    auto settingsLink = action(t("Software and driver utilities can be enabled or disabled in Settings",
                                 "يمكن تفعيل البرامج وأدوات التعريفات أو إخفاؤها من الإعدادات"),
                               body);
    settingsLink->setObjectName(QStringLiteral("link"));
    connect(settingsLink, &QPushButton::clicked, this, [this]() { expert(5); });
    box->addWidget(settingsLink);
    box->addSpacing(10);
    if (m_additional)
    {
        auto fields = action(t("Additional information fields ▾", "حقول المعلومات الإضافية ▾"), body);
        fields->setObjectName(QStringLiteral("driverFieldPicker"));
        connect(
            fields, &QPushButton::clicked, this,
            [this, fields]()
            {
                RoundedDialog panel(this);
                panel.setObjectName(QStringLiteral("driverFieldPanel"));
                panel.setWindowTitle(t("Additional information", "المعلومات الإضافية"));
                panel.resize(qMin(560, width() - 60), 420);
                auto frame = new QVBoxLayout(&panel);
                frame->setContentsMargins(0, 0, 0, 0);
                frame->setSpacing(0);
                frame->addWidget(new DialogHeader(t("Additional information", "المعلومات الإضافية"),
                                                  QStringLiteral("layers"), &panel, m_arabic));
                auto content = new QWidget(&panel);
                auto layout = new QVBoxLayout(content);
                layout->setContentsMargins(24, 18, 24, 22);
                layout->setSpacing(18);
                frame->addWidget(content);
                layout->addWidget(copy(t("Choose the details shown beneath each driver version.",
                                         "اختر التفاصيل التي تظهر أسفل كل إصدار للتعريف."),
                                       content, "caption"));
                const char *keys[] = {"version", "date", "deviceId", "vendor", "inf", "section", "os"};
                const char *en[] = {"Version",  "Date",        "Hardware ID", "Manufacturer",
                                    "INF file", "INF section", "Windows"};
                const char *ar[] = {"الإصدار", "التاريخ", "معرّف الجهاز", "الشركة",
                                    "ملف INF", "قسم INF", "ويندوز"};
                QVector<QCheckBox *> choices;
                auto grid = new QGridLayout;
                grid->setSpacing(16);
                for (int i = 0; i < 7; ++i)
                {
                    auto option = new QCheckBox(t(en[i], ar[i]), content);
                    option->setMinimumHeight(38);
                    option->setChecked(
                        m_settings
                            ->value(QStringLiteral("driverpack/view/") + QString::fromLatin1(keys[i]), i < 3)
                            .toBool());
                    choices << option;
                    grid->addWidget(option, i / 2, i % 2);
                }
                layout->addLayout(grid);
                auto commands = new QHBoxLayout;
                auto reset = action(t("Restore default", "استعادة الافتراضي"), content);
                connect(reset, &QPushButton::clicked, &panel,
                        [choices]()
                        {
                            for (int i = 0; i < choices.size(); ++i)
                                choices[i]->setChecked(i < 3);
                        });
                commands->addWidget(reset);
                auto apply = action(t("Apply", "تطبيق"), content);
                connect(apply, &QPushButton::clicked, &panel, &QDialog::accept);
                commands->addWidget(apply);
                layout->addLayout(commands);
                if (panel.exec() == QDialog::Accepted)
                {
                    for (int i = 0; i < choices.size(); ++i)
                        m_settings->setValue(QStringLiteral("driverpack/view/") +
                                                 QString::fromLatin1(keys[i]),
                                             choices[i]->isChecked());
                    expert(0);
                }
            });
        box->addWidget(fields, 0, Qt::AlignTrailing);
    }
    QVector<int> missing, updates, current, broken;
    for (int i = 0; i < m_catalog->size(); ++i)
    {
        const auto &e = m_catalog->entryRef(i);
        bool has = false;
        for (const auto &c : e.candidates)
            if (c.engineToken)
                has = true;
        if (!e.installedDriverPresent && has)
            missing << i;
        else if (driverStatus(e) == QStringLiteral("Upgrade"))
            updates << i;
        else if (e.device.hasProblem)
            broken << i;
        else if (m_showInstalled && has)
            current << i;
    }
    auto list = [&](const QString &title, const QVector<int> &rows)
    {
        if (rows.isEmpty())
            return;
        section(box, title, rows);
        for (int i : rows)
        {
            auto family = new SurfaceCard(body);
            family->setObjectName(QStringLiteral("driverFamily"));
            family->setProperty("tone", QStringLiteral("blue"));
            auto familyBox = new QVBoxLayout(family);
            familyBox->setContentsMargins(10, 6, 10, 10);
            familyBox->setSpacing(4);
            familyBox->setAlignment(Qt::AlignTop);
            driverRow(familyBox, i);
            const auto e = m_catalog->entry(i);
            auto alternatives = new DriverAlternatives(family);
            auto versions = new QVBoxLayout(alternatives->canvas());
            versions->setContentsMargins(m_arabic ? 4 : 48, 0, m_arabic ? 48 : 4, 0);
            versions->setSpacing(4);
            alternatives->populate = [this, i, versions, e]()
            {
                for (int j = 0; j < e.candidates.size(); ++j)
                    driverRow(versions, i, j);
            };
            familyBox->addWidget(alternatives);
            alternatives->setOpen(m_expanded.contains(e.device.instanceId), false);
            box->addWidget(family);
        }
    };
    list(t("Driver installation", "تثبيت التعريفات الناقصة"), missing);
    list(t("Driver updates", "تحديثات التعريفات"), updates);
    list(t("Installed and alternative drivers", "التعريفات المثبتة والبديلة"), current);
    list(t("Devices with a problem", "أجهزة بها مشكلة"), broken);
    if (missing.isEmpty() && updates.isEmpty() && !m_showInstalled)
    {
        int matched = 0, alternatives = 0;
        QString newest;
        for (const auto &entry : m_catalog->entries())
        {
            bool has = false;
            for (const auto &candidate : entry.candidates)
                if (candidate.engineToken)
                {
                    has = true;
                    ++alternatives;
                    if (candidate.date > newest)
                        newest = candidate.date;
                }
            if (has)
                ++matched;
        }
        auto coverage = new SurfaceCard(body);
        coverage->setObjectName(QStringLiteral("libraryCoverage"));
        coverage->setProperty("tone", QStringLiteral("teal"));
        auto content = new QHBoxLayout(coverage);
        content->setContentsMargins(22, 18, 22, 20);
        auto labels = new QVBoxLayout;
        labels->addWidget(copy(t("No newer matches in this library", "لا توجد مطابقات أحدث في هذه المكتبة"),
                               coverage, "heading"));
        labels->addWidget(copy(
            t("%1 devices with matches · %2 catalog alternatives", "%1 جهاز له مطابقات · %2 بديل في الفهرس")
                .arg(matched)
                .arg(alternatives),
            coverage, "caption"));
        if (!newest.isEmpty())
            labels->addWidget(
                copy(t("Newest matching catalog date: %1. Newer installed drivers are kept.",
                       "أحدث تاريخ للمطابقات في الفهرس: %1. يتم الاحتفاظ بالتعريفات الأحدث المثبتة.")
                         .arg(newest),
                     coverage, "caption"));
        content->addLayout(labels, 1);
        auto view = action(t("View alternatives", "عرض البدائل"), coverage);
        view->setEnabled(matched > 0);
        connect(view, &QPushButton::clicked, this,
                [this]()
                {
                    m_showInstalled = true;
                    expert(0);
                });
        content->addWidget(view, 0, Qt::AlignVCenter);
        box->addWidget(coverage);
    }
    if (m_settings->value(QStringLiteral("driverpack/showSoftware"), true).toBool())
    {
        section(box, t("Driver toolkits", "أدوات التعريفات"));
        for (int i = 0; i < m_programs.size(); ++i)
            if (m_programs[i].toolkit && !m_programs[i].excluded)
                programRow(box, i, true);
    }
    m_footerPrimary = action(QString(), body, true);
    m_footerPrimary->setMaximumWidth(500);
    connect(m_footerPrimary, &QPushButton::clicked, this, [this]() { startInstall(); });
    box->addWidget(m_footerPrimary, 0, Qt::AlignLeading);
    box->addStretch();
    updateCounts();
}
void DriverPackShell::driverRow(QVBoxLayout *box, int row, int candidate)
{
    const auto e = m_catalog->entry(row);
    const bool alternative = candidate >= 0;
    int chosen = alternative ? candidate : m_remoteChoices.value(e.device.instanceId, e.chosenCandidate);
    if (!alternative && chosen < 1)
        for (int j = 1; j < e.candidates.size(); ++j)
            if (e.candidates[j].valid)
            {
                chosen = j;
                if (e.candidates[j].recommended)
                    break;
            }
    DriverCandidate c;
    if (chosen >= 0 && chosen < e.candidates.size())
        c = e.candidates[chosen];
    auto line = new ClickRow(box->parentWidget());
    line->setMinimumHeight(alternative ? 76 : 96);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    line->setObjectName(QStringLiteral("driverChoiceRow"));
    auto layout = new QHBoxLayout(line);
    layout->setContentsMargins(alternative ? 34 : 15, 14, 15, 14);
    layout->setSpacing(20);
    auto tick = alternative ? static_cast<QAbstractButton *>(new QRadioButton(line))
                            : static_cast<QAbstractButton *>(new QCheckBox(line));
    if (auto radio = qobject_cast<QRadioButton *>(tick))
        radio->setAutoExclusive(false);
    const bool can =
        (c.engineToken && c.valid) || (alternative && candidate == 0 && reinstallCandidateIndex(e) > 0);
    tick->setProperty("driverRowIndex", row);
    tick->setProperty("driverCandidateIndex", alternative ? candidate : -1);
    tick->setEnabled(can);
    if (!can)
        line->setCursor(Qt::ArrowCursor);
    tick->setToolTip(t("Select this driver", "تحديد هذا التعريف"));
    tick->setChecked(alternative ? ((e.checked && e.chosenCandidate == candidate) ||
                                    (m_remote.contains(e.device.instanceId) &&
                                     m_remoteChoices.value(e.device.instanceId, -1) == candidate))
                                 : (e.checked || m_remote.contains(e.device.instanceId)));
    connect(tick, &QAbstractButton::toggled, this,
            [this, row, candidate, alternative, e](bool value)
            {
                if (alternative && value)
                    m_remoteChoices[e.device.instanceId] = candidate;
                setDriver(row, value);
            });
    if (alternative)
        layout->addWidget(tick);
    else
    {
        auto expand = new DriverChevron(m_expanded.contains(e.device.instanceId), line);
        expand->setObjectName(QStringLiteral("driverChevron"));
        expand->setProperty("driverKey", e.device.instanceId);
        expand->setToolTip(t("Show or hide driver versions", "عرض إصدارات التعريف أو إخفاؤها"));
        connect(expand, &QAbstractButton::clicked, this,
                [this, e, line, expand]()
                {
                    const bool open = !m_expanded.contains(e.device.instanceId);
                    if (open)
                        m_expanded.insert(e.device.instanceId);
                    else
                        m_expanded.remove(e.device.instanceId);
                    expand->setOpen(open);
                    QWidget *family = line->parentWidget();
                    if (auto panel = family->findChild<QWidget *>(QStringLiteral("driverAlternatives")))
                        static_cast<DriverAlternatives *>(panel)->setOpen(open, true);
                });
        layout->addWidget(expand);
        auto icon = copy(QString(), line);
        icon->setPixmap(
            VectorIcons::icon(deviceIcon(e.device.deviceClass), QColor(QStringLiteral("#20b6dc")), 28)
                .pixmap(28, 28));
        icon->setFixedWidth(32);
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(icon);
    }
    auto names = new QVBoxLayout;
    const QString klass = e.device.deviceClass.toLower();
    QString category = t("Device", "جهاز");
    if (klass == QStringLiteral("display"))
        category = t("Video card", "كارت شاشة");
    else if (klass == QStringLiteral("net"))
        category = t("Network card", "كارت شبكة");
    else if (klass.contains(QStringLiteral("media")) || klass.contains(QStringLiteral("audio")))
        category = t("Sound card", "كارت صوت");
    else if (klass == QStringLiteral("system"))
        category = t("System device", "جهاز نظام");
    else if (klass.contains(QStringLiteral("hid")) || klass.contains(QStringLiteral("mouse")))
        category = t("Input device", "جهاز إدخال");
    else if (klass.contains(QStringLiteral("monitor")))
        category = t("Monitor", "شاشة");
    else if (klass.contains(QStringLiteral("storage")) || klass == QStringLiteral("hdc"))
        category = t("Controller", "وحدة تحكم");
    else if (klass.contains(QStringLiteral("software")))
        category = t("Software component", "مكوّن برمجي");
    if (alternative)
    {
        QStringList heading;
        if (m_settings->value(QStringLiteral("driverpack/view/version"), true).toBool())
            heading << t("Version: ", "الإصدار: ") + c.version;
        if (m_settings->value(QStringLiteral("driverpack/view/date"), true).toBool())
            heading << t("Date: ", "التاريخ: ") + c.date;
        auto label = copy(ltr(heading.join(QStringLiteral(" / "))), line);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        names->addWidget(label);
        QStringList details;
        if (m_settings->value(QStringLiteral("driverpack/view/deviceId"), true).toBool())
            details << c.hardwareId;
        if (m_additional)
        {
            if (m_settings->value(QStringLiteral("driverpack/view/vendor"), false).toBool())
                details << c.provider;
            if (m_settings->value(QStringLiteral("driverpack/view/inf"), false).toBool())
                details << c.inf;
            if (m_settings->value(QStringLiteral("driverpack/view/section"), false).toBool())
                details << c.section;
            if (m_settings->value(QStringLiteral("driverpack/view/os"), false).toBool())
                details << t("Minimum Windows build: ", "الحد الأدنى لبناء ويندوز: ") +
                               QString::number(c.minimumBuild);
        }
        auto detail = copy(ltr(details.join(QLatin1Char('\n'))), line, "caption");
        detail->setAttribute(Qt::WA_TransparentForMouseEvents);
        names->addWidget(detail);
    }
    else
    {
        auto title = copy(category + QStringLiteral(" · ") + e.device.manufacturer, line);
        title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        names->addWidget(title);
        auto name = copy(e.device.name, line, "caption");
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        names->addWidget(name);
        if (!c.local && c.engineToken)
            names->addWidget(copy(t("[Not downloaded]", "[غير محمّل]"), line, "caption"));
    }
    layout->addLayout(names, 1);
    if (alternative)
    {
        auto recommended = copy(candidate == 0                       ? t("Current driver", "التعريف الحالي")
                                : isReinstallCandidate(e, candidate) ? t("Reinstall", "إعادة تثبيت")
                                : (c.statusBits & 0x80u)             ? t("Downgrade", "إصدار أقدم")
                                : c.recommended                      ? t("Recommended", "موصى به")
                                                                     : QString(),
                                line, "caption");
        recommended->setMinimumWidth(135);
        layout->addWidget(recommended);
        auto stars = copy(candidate == 0 ? QString(5, QChar(0x2606))
                                         : QString(matchPriorityStars(e, candidate), QChar(0x2605)),
                          line, "caption");
        stars->setMinimumWidth(95);
        stars->setToolTip(t("Priority derived from DriverPack's catalog rank, not a safety rating.",
                            "أولوية مشتقة من ترتيب كتالوج درايفر باك وليست تقييم أمان."));
        layout->addWidget(stars);
    }
    else
    {
        auto current =
            copy(t("Current version", "الإصدار الحالي") + QLatin1Char('\n') + e.device.driverVersion, line,
                 "caption");
        current->setMinimumWidth(130);
        layout->addWidget(current);
        QString state = driverStatus(e);
        if (state == QStringLiteral("Upgrade"))
            state = t("Update", "تحديث");
        else if (state == QStringLiteral("Missing"))
            state = t("Installation", "تثبيت");
        else if (state == QStringLiteral("Reinstall"))
            state = t("Reinstall", "إعادة تثبيت");
        else if (state == QStringLiteral("Downgrade"))
            state = t("Downgrade", "إصدار أقدم");
        else
            state = t("Installed", "مثبت");
        auto available = copy(state + QLatin1Char('\n') + c.version, line, "caption");
        available->setMinimumWidth(115);
        layout->addWidget(available);
        layout->addWidget(tick);
    }
    auto more = action(QStringLiteral("⋮"), line);
    more->setFixedWidth(34);
    more->setObjectName(QStringLiteral("rowIconAction"));
    more->setProperty("ownHover", true);
    more->setToolTip(t("Driver actions", "إجراءات التعريف"));
    layout->addWidget(more);
    connect(more, &QPushButton::clicked, this,
            [this, row, chosen, e, c, more]()
            {
                ProjectMenu menu(this);
                auto info = menu.addAction(t("Driver information", "معلومات التعريف"));
                connect(info, &QAction::triggered, this, [this, row, chosen]() { details(row, chosen); });
                auto properties = menu.addAction(t("Device properties", "خصائص الجهاز"));
                connect(properties, &QAction::triggered, this,
                        [this, e]()
                        {
                            windowsTool(QStringLiteral("rundll32.exe"),
                                        QStringList() << QStringLiteral("devmgr.dll,DeviceProperties_RunDLL")
                                                      << QStringLiteral("/DeviceID") << e.device.instanceId);
                        });
                auto select = menu.addAction(t("Select this driver", "تحديد هذا التعريف"));
                select->setEnabled(c.engineToken && c.valid);
                connect(select, &QAction::triggered, this,
                        [this, row, chosen, e]()
                        {
                            m_remoteChoices[e.device.instanceId] = chosen;
                            setDriver(row, true);
                            expert(0);
                        });
                auto search =
                    menu.addAction(t("Find this driver in the catalog", "البحث عن التعريف في الكتالوج"));
                connect(search, &QAction::triggered, this,
                        [this, e]()
                        {
                            if (m_capture)
                                return;
                            QUrl url(QStringLiteral("https://drp.su/en/hwids/") +
                                     QString::fromLatin1(QUrl::toPercentEncoding(e.device.hardwareId)));
                            QDesktopServices::openUrl(url);
                        });
                menu.exec(more->mapToGlobal(QPoint(0, more->height())));
            });
    tick->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    line->setProperty("selectedDriver", tick->isChecked());
    line->clicked = [tick]()
    {
        if (tick->isEnabled())
            tick->setChecked(!tick->isChecked());
    };
    line->doubled = [this, row, candidate]() { details(row, candidate); };
    line->ensurePolished();
    if (line->layout())
        line->layout()->activate();
    line->setFixedHeight(qMax(alternative ? 76 : 96, line->sizeHint().height()));
    box->addWidget(line);
}

void DriverPackShell::toggleSoftware(const QString &id, bool value)
{
    if (value)
        m_programSelection.insert(id);
    else
        m_programSelection.remove(id);
    if (value && m_mode == 0)
        m_mode = 1;
    updateCounts();
}
void DriverPackShell::softwareSection(QVBoxLayout *box, const QString &title, const QVector<int> &rows)
{
    section(box, title);
    auto header = box->itemAt(box->count() - 1)->widget();
    auto all = new GroupCheck(header);
    all->setObjectName(QStringLiteral("softwareGroupToggle"));
    QStringList ids;
    for (int index : rows)
    {
        const auto &program = m_programs[index];
        if (!program.installed &&
            (!program.localPath.isEmpty() || DriverPackSoftware::allowedUrl(program.url)))
            ids << program.id;
    }
    all->setProperty("softwareIds", ids);
    all->setToolTip(t("Select or clear available programs", "تحديد البرامج المتاحة أو إلغاء تحديدها"));
    static_cast<QHBoxLayout *>(header->layout())->addWidget(all);
    connect(all, &QCheckBox::clicked, this,
            [this, all, ids]()
            {
                const bool select = all->checkState() == Qt::Checked;
                for (const auto &id : ids)
                    if (select)
                        m_programSelection.insert(id);
                    else
                        m_programSelection.remove(id);
                updateCounts();
            });
}
void DriverPackShell::programRow(QVBoxLayout *box, int index, bool toolkit)
{
    const auto p = m_programs[index];
    auto row = new ClickRow(box->parentWidget());
    row->setMinimumHeight(94);
    auto line = new QHBoxLayout(row);
    line->setContentsMargins(15, 14, 15, 14);
    line->setSpacing(14);
    auto checked = new QCheckBox(row);
    checked->setProperty("softwareId", p.id);
    checked->setChecked(m_programSelection.contains(p.id));
    checked->setToolTip(t("Select %1", "تحديد %1").arg(p.name));
    checked->setEnabled(!p.installed && (!p.localPath.isEmpty() || DriverPackSoftware::allowedUrl(p.url)));
    connect(checked, &QCheckBox::toggled, this, [this, p](bool value) { toggleSoftware(p.id, value); });
    line->addWidget(checked);
    auto programIcon = copy(QString(), row);
    programIcon->setObjectName(QStringLiteral("programIcon"));
    QPixmap logo(DriverPackSoftware::iconPath(m_library, p));
    if (logo.isNull())
    {
        const QString installer = DriverPackSoftware::localProgram(m_library, p, true);
        if (!installer.isEmpty())
        {
            static QHash<QString, QPixmap> cache;
            const QFileInfo file(installer);
            const int pixels = qRound(36 * programIcon->devicePixelRatioF());
            const QString key = installer + QString::number(file.lastModified().toMSecsSinceEpoch()) +
                                QString::number(pixels);
            if (!cache.contains(key))
            {
                if (cache.size() > 128)
                    cache.clear();
                QFileIconProvider provider;
                cache.insert(key, provider.icon(file).pixmap(pixels, pixels));
            }
            logo = cache.value(key);
        }
    }
    if (!logo.isNull())
    {
        const qreal dpr = programIcon->devicePixelRatioF();
        logo = logo.scaled(qRound(34 * dpr), qRound(34 * dpr), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        logo.setDevicePixelRatio(dpr);
        programIcon->setPixmap(logo);
    }
    else
    {
        const qreal dpr = programIcon->devicePixelRatioF();
        const int pixels = qRound(30 * dpr);
        logo = VectorIcons::icon(p.toolkit ? QStringLiteral("settings") : QStringLiteral("apps"),
                                 QColor("#00aaa9"), pixels)
                   .pixmap(pixels, pixels);
        logo.setDevicePixelRatio(dpr);
        programIcon->setPixmap(logo);
    }
    programIcon->setFixedSize(46, 46);
    programIcon->setAlignment(Qt::AlignCenter);
    programIcon->setStyleSheet(QStringLiteral("background:%1;border-radius:13px;")
                                   .arg(m_dark ? QStringLiteral("#294c5c") : QStringLiteral("#eaf7f7")));
    programIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    programIcon->setToolTip(p.name);
    line->addWidget(programIcon);
    auto nameBox = new QVBoxLayout;
    auto name = copy(ltr(p.name), row);
    name->setStyleSheet(QStringLiteral("font-size:19px;"));
    nameBox->addWidget(name);
    nameBox->addWidget(copy(p.publisher.isEmpty() && toolkit
                                ? t("Device and driver utilities", "أدوات الجهاز والتعريفات")
                                : p.publisher,
                            row, "caption"));
    auto names = new QWidget(row);
    names->setLayout(nameBox);
    names->setMinimumWidth(130);
    names->setMaximumWidth(260);
    names->setAttribute(Qt::WA_TransparentForMouseEvents);
    line->addWidget(names, 3);
    if (!toolkit)
    {
        auto stars = copy(p.rating ? QString(p.rating, QChar(0x2605)) : QStringLiteral("—"), row, "caption");
        stars->setMinimumWidth(90);
        stars->setToolTip(t("Rating recorded in this DriverPack software catalog",
                            "التقييم المسجل في كتالوج برامج درايفر باك"));
        line->addWidget(stars, 1);
    }
    QString description = p.description;
    if (m_arabic || description.isEmpty())
    {
        const QMap<QString, QString> descriptions = {
            {QStringLiteral("Archiver"),
             t("Archive and extract compressed files", "ضغط الملفات وفك الأرشيفات")},
            {QStringLiteral("Browser"), t("Web browser", "متصفح للإنترنت")},
            {QStringLiteral("Antivirus"), t("Antivirus software", "برنامج لمكافحة الفيروسات")},
            {QStringLiteral("Player"), t("Play audio and video files", "تشغيل ملفات الصوت والفيديو")},
            {QStringLiteral("Viewer"), t("View documents and images", "عرض المستندات والصور")},
            {QStringLiteral("Backup"),
             t("Back up files and system data", "نسخ الملفات وبيانات النظام احتياطيًا")},
            {QStringLiteral("Messenger"), t("Messaging and calls", "المراسلة والمكالمات")}};
        description = descriptions.value(p.category, p.name);
        if (p.id == QStringLiteral("11"))
            description = t("Audio and video codecs", "حزم ترميز الصوت والفيديو");
        if (p.id == QStringLiteral("12") || p.id == QStringLiteral("13"))
            description = t("Messaging and calls", "المراسلة والمكالمات");
    }
    auto desc = copy(description, row, "caption");
    desc->setAttribute(Qt::WA_TransparentForMouseEvents);
    line->addWidget(desc, 4);
    auto legal = new QWidget(row);
    legal->setFixedWidth(108);
    const QString iconStyle=QStringLiteral("QPushButton{background:transparent;border:1px solid transparent;border-radius:10px;padding:0;} QPushButton:hover:enabled{background:%1;border-color:#62c6c2;} QPushButton:pressed:enabled{background:%2;}").arg(m_dark?QStringLiteral("#315d70"):QStringLiteral("#e2f3f1"),m_dark?QStringLiteral("#3c7182"):QStringLiteral("#ccebe7"));
    auto links = new QHBoxLayout(legal);
    links->setContentsMargins(0, 0, 0, 0);
    links->setSpacing(6);
    links->setAlignment(Qt::AlignTrailing);
    for (int k = 0; k < 2; ++k)
    {
        const QString address = k ? p.policy : p.license;
        auto link = action(QString(), legal);
        link->setObjectName(QStringLiteral("rowIconAction"));
        link->setProperty("ownHover", true);
        link->setFixedSize(32, 36);
        link->setProperty("projectAction",false);
        link->setStyleSheet(iconStyle);
        link->setMinimumHeight(0);
        link->setIcon(
            VectorIcons::icon(k ? QStringLiteral("shield") : QStringLiteral("info"), QColor("#00aaa9"), 19));
        link->setToolTip(k ? t("Privacy Policy", "سياسة الخصوصية")
                           : t("License Agreement", "اتفاقية الترخيص"));
        link->setVisible(!address.isEmpty());
        connect(link, &QPushButton::clicked, this,
                [this, address]()
                {
                    if (!m_capture && DriverPackSoftware::allowedUrl(address))
                        QDesktopServices::openUrl(QUrl(address));
                });
        links->addWidget(link);
    }

    auto edit = action(QString(), legal);
    edit->setObjectName(QStringLiteral("rowIconAction"));
    edit->setProperty("ownHover", true);
    edit->setProperty("projectAction", false);
    edit->setStyleSheet(iconStyle);
    edit->setFixedSize(32, 36);
    edit->setMinimumHeight(0);
    edit->setIcon(VectorIcons::icon(QStringLiteral("edit"), QColor("#00aaa9"), 19));
    edit->setToolTip(t("Edit or remove program", "تعديل البرنامج أو حذفه"));
    edit->setProperty("editSoftwareId", p.id);
    connect(edit, &QPushButton::clicked, this, [this, index]() { addProgramDialog(false, index); });
    links->addWidget(edit);
    line->addWidget(legal);
    auto state = action(p.installed             ? t("✓  Installed", "✓  مثبت")
                        : p.localPath.isEmpty() ? t("Not downloaded", "غير محمّل")
                                                : t("Install", "تثبيت"),
                        row);
    state->setFixedWidth(qMax(150, state->fontMetrics().width(state->text()) + 44));
    state->setEnabled(!p.installed && (!p.localPath.isEmpty() || DriverPackSoftware::allowedUrl(p.url)));
    connect(state, &QPushButton::clicked, this,
            [this, p]()
            {
                m_singleSoftware = p.id;
                startInstall();
                m_singleSoftware.clear();
            });
    line->addWidget(state);
    for (auto label : row->findChildren<QLabel *>())
        if (label != programIcon)
            label->setStyleSheet(label->styleSheet() + QStringLiteral(";background:transparent;"));
    names->setStyleSheet(QStringLiteral("background:transparent;"));
    legal->setStyleSheet(QStringLiteral("background:transparent;"));
    if (p.installed)
    {
        row->setCursor(Qt::ArrowCursor);
        state->setCursor(Qt::ArrowCursor);
    }
    row->clicked = [checked]()
    {
        if (checked->isEnabled())
            checked->setChecked(!checked->isChecked());
    };
    box->addWidget(row);
}
void DriverPackShell::buildSoftware(QWidget *page, bool security)
{
    QVBoxLayout *box;
    auto body = pageBody(page, &box);
    box->setSpacing(12);
    auto heading = new QHBoxLayout;
    heading->addWidget(copy(security ? t("Important security software", "برامج الحماية")
                                     : t("All useful software in one place", "البرامج المفيدة في مكان واحد"),
                            body, "title"),
                       1);
    auto add = action(t("Add a program…", "إضافة برنامج…"), body);
    add->setObjectName(QStringLiteral("addSoftware"));
    add->setIcon(VectorIcons::icon(QStringLiteral("apps"), QColor("#00aaa9"), 20));
    connect(add, &QPushButton::clicked, this, [this]() { addProgramDialog(); });
    heading->addWidget(add);
    box->addLayout(heading);
    box->addSpacing(20);
    auto header = new QHBoxLayout;
    m_softwarePrimary = action(QString(), body, true);
    m_softwarePrimary->setMinimumWidth(420);
    m_softwarePrimary->setIcon(VectorIcons::icon(QStringLiteral("spark"), Qt::white, 34));
    m_softwarePrimary->setIconSize(QSize(34, 34));
    header->addWidget(m_softwarePrimary);
    connect(m_softwarePrimary, &QPushButton::clicked, this,
            [this]()
            {
                if (m_programSelection.isEmpty())
                {
                    showMessage(t("Select programs", "تحديد البرامج"),
                                t("Choose one or more programs from the list first.",
                                  "اختر برنامجًا واحدًا أو أكثر من القائمة أولًا."));
                    return;
                }
                m_softwareOnly = true;
                startInstall();
                m_softwareOnly = false;
            });
    header->addSpacing(30);
    header->addWidget(
        copy(t("Choose the programs you need, then review their local setup wizards. Nothing is added "
               "automatically.",
               "اختر البرامج التي تحتاجها ثم راجع مثبتاتها المحلية. لا يُضاف أي برنامج تلقائيًا."),
             body, "caption"),
        1);
    box->addLayout(header);
    QVector<int> groupRows;
    for (int i = 0; i < m_programs.size(); ++i)
        if (!m_programs[i].excluded && !m_programs[i].toolkit &&
            (!security || m_programs[i].category == QStringLiteral("Antivirus")))
            groupRows << i;
    softwareSection(box, t("Recommended software", "البرامج المتاحة"), groupRows);
    int count = 0;
    for (int i = 0; i < m_programs.size(); ++i)
    {
        const auto &p = m_programs[i];
        if (p.excluded || p.toolkit || (security && p.category != QStringLiteral("Antivirus")))
            continue;
        programRow(box, i);
        ++count;
    }
    if (!count)
        box->addWidget(copy(t("No supported software records were found in this library.",
                              "لم يُعثر على بيانات برامج مدعومة في المكتبة."),
                            body, "largeCaption"));
    box->addStretch();
    updateCounts();
}
void DriverPackShell::addProgramDialog(bool security, int editIndex)
{
    const bool editing = editIndex >= 0 && editIndex < m_programs.size();
    const auto original = editing ? m_programs[editIndex] : DriverPackSoftware::Program();
    const int returnPage = m_page;
    RoundedDialog dialog(this);
    dialog.setObjectName(QStringLiteral("addProgramDialog"));
    dialog.setWindowTitle(t("Program editor", "تعديل البرامج"));
    dialog.resize(qMin(650, width() - 60), qMin(660, height() - 60));
    auto frame = new QVBoxLayout(&dialog);
    frame->setContentsMargins(0, 0, 0, 0);
    frame->setSpacing(0);
    frame->addWidget(
        new DialogHeader(t("Program editor", "تعديل البرامج"), QStringLiteral("apps"), &dialog, m_arabic));
    auto content = new QWidget;
    auto box = new QVBoxLayout(content);
    box->setContentsMargins(24, 18, 24, 22);
    box->setSpacing(14);
    frame->addWidget(content, 1);
    box->addWidget(
        copy(t("Place the installer in this library's programs folder, or enter its publisher URL. Adding a "
               "record does not install anything.",
               "ضع ملف التثبيت داخل مجلد programs بالمكتبة، أو أدخل رابط الناشر. إضافة البرنامج لا تثبته."),
             content, "caption"));
    auto formHost = new QWidget;
    formHost->setObjectName(QStringLiteral("addProgramCanvas"));
    auto form = new QGridLayout(formHost);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(8);
    form->setColumnStretch(0, 1);
    form->setColumnStretch(1, 1);
    int fieldIndex = 0;
    auto field = [&](const QString &label, const QString &hint)
    {
        auto edit = new QLineEdit(formHost);
        edit->setPlaceholderText(hint);
        const int column = fieldIndex % 2, row = (fieldIndex / 2) * 2;
        ++fieldIndex;
        form->addWidget(copy(label, formHost, "caption"), row, column);
        form->addWidget(edit, row + 1, column);
        return edit;
    };
    auto name = field(t("Name", "الاسم"), t("Program name", "اسم البرنامج"));
    name->setObjectName(QStringLiteral("customProgramName"));
    auto publisher = field(t("Publisher", "الناشر"), QString());
    auto version = field(t("Version", "الإصدار"), QString());
    auto filename = field(t("Installer file", "ملف التثبيت"), QStringLiteral("my-app.exe"));
    auto url = field(t("Publisher URL", "رابط الناشر"), QStringLiteral("https://…"));
    auto icon = field(t("Icon", "الأيقونة"), QStringLiteral("programs/icons/my-app.png"));
    auto description = field(t("Description", "الوصف"), QString());
    auto category = new SearchableLanguageCombo(content);
    category->setObjectName(QStringLiteral("customProgramCategory"));
    category->addItem(t("Utility", "أدوات"), QStringLiteral("Utility"));
    category->addItem(t("Antivirus", "مكافحة الفيروسات"), QStringLiteral("Antivirus"));
    category->addItem(t("Browser", "متصفح"), QStringLiteral("Browser"));
    category->addItem(t("Player", "مشغل وسائط"), QStringLiteral("Player"));
    for (const QString &value :
         {QStringLiteral("Viewer"), QStringLiteral("Backup"), QStringLiteral("Messenger"),
          QStringLiteral("System"), QStringLiteral("Archiver"), QStringLiteral("Codec"), QStringLiteral("ImageViewer"), QStringLiteral("Support")})
        category->addItem(value, value);
    if (security)
        category->setCurrentIndex(1);
    if (editing)
    {
        name->setText(original.name);
        publisher->setText(original.publisher);
        version->setText(original.version);
        filename->setText(original.fileName);
        url->setText(original.url);
        icon->setText(original.icon);
        description->setText(original.description);
        int at = category->findData(original.category);
        if (at < 0)
        {
            category->addItem(original.category, original.category);
            at = category->count() - 1;
        }
        category->setCurrentIndex(at);
    }
    form->addWidget(copy(t("Category", "الفئة"), formHost, "caption"), 6, 1);
    form->addWidget(category, 7, 1);
    auto scroll = new QScrollArea(content);
    scroll->setObjectName(QStringLiteral("addProgramScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumSize(0, 0);
    scroll->setWidget(formHost);
    box->addWidget(scroll, 1);
    auto commands = new QHBoxLayout;
    auto folder = action(t("Open programs folder", "فتح مجلد البرامج"), content);
    connect(folder, &QPushButton::clicked, this,
            [this]()
            {
                if (m_capture)
                    return;
                const QString folder = QDir(m_library).filePath(QStringLiteral("programs"));
                if (QDir().mkpath(folder))
                    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
            });
    commands->addWidget(folder);
    if (editing)
    {
        auto removeFile =
            new QCheckBox(t("Also delete the local installer", "حذف ملف التثبيت المحلي أيضًا"), content);
        removeFile->setEnabled(!original.localPath.isEmpty());
        box->addWidget(removeFile);
        auto remove = action(t("Remove program", "حذف البرنامج"), content);
        commands->addWidget(remove);
        connect(remove, &QPushButton::clicked, &dialog,
                [=, &dialog]()
                {
                    if (m_capture ||
                        !confirm(t("Remove program", "حذف البرنامج"),
                                 t("Remove %1 from the catalog? This does not uninstall it from Windows.",
                                   "حذف %1 من الكتالوج؟ لن يتم إلغاء تثبيته من ويندوز.")
                                     .arg(original.name)))
                        return;
                    try
                    {
                        const QString warning = DriverPackSoftware::removeProgram(m_library, original.id,
                                                                                  removeFile->isChecked());
                        m_programSelection.remove(original.id);
                        if (!warning.isEmpty())
                            showMessage(t("Program removed", "تم حذف البرنامج"), warning);
                        dialog.accept();
                    }
                    catch (const std::exception &error)
                    {
                        showMessage(t("Remove program", "حذف البرنامج"), QString::fromUtf8(error.what()));
                    }
                });
    }
    auto save = action(t("Save program", "حفظ البرنامج"), content);
    save->setObjectName(QStringLiteral("saveCustomProgram"));
    commands->addWidget(save);
    box->addLayout(commands);
    connect(save, &QPushButton::clicked, &dialog,
            [=, &dialog]()
            {
                if (m_capture)
                    return;
                DriverPackSoftware::Program program = original;
                if (!editing)
                    program.id =
                        QStringLiteral("user-") +
                        QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'));
                program.name = name->text().trimmed();
                program.publisher = publisher->text().trimmed();
                program.version = version->text().trimmed();
                program.fileName = filename->text().trimmed();
                program.url = url->text().trimmed();
                program.icon = icon->text().trimmed();
                program.description = description->text().trimmed();
                program.category = category->currentData().toString();
                try
                {
                    DriverPackSoftware::saveCustomProgram(m_library, program);
                    if (editing)
                        m_programSelection.remove(original.id);
                    dialog.accept();
                }
                catch (const std::exception &error)
                {
                    showMessage(t("Add program", "إضافة برنامج"), QString::fromUtf8(error.what()));
                }
            });
    if (dialog.exec() == QDialog::Accepted)
    {
        loadSoftware();
        expert(returnPage);
    }
}
void DriverPackShell::buildProtection(QWidget *page)
{
    QVBoxLayout *box;
    auto body = pageBody(page, &box);
    auto hero = new AccentCard(body);
    auto heroBox = new QVBoxLayout(hero);
    heroBox->setContentsMargins(26, 24, 26, 24);
    heroBox->setSpacing(14);
    hero->setStyleSheet(QStringLiteral("QLabel{color:white;} QLabel#caption{color:#c8ecec;}"));
    heroBox->addWidget(
        copy(t("Protection and cleanup of your computer", "حماية الكمبيوتر وتنظيفه"), hero, "title"));
    heroBox->addWidget(copy(t("Review installed software and use the protection tools available in Windows.",
                              "راجع البرامج المثبتة واستخدم أدوات الحماية المتاحة في ويندوز."),
                            hero, "caption"));
    box->addWidget(hero);
    auto commands = new QHBoxLayout;
    auto security = action(t("Protect and clean the computer", "حماية الكمبيوتر وتنظيفه"), body, true);
    commands->addWidget(security);
    connect(security, &QPushButton::clicked, this,
            [this]()
            {
#ifdef Q_OS_WIN
                if (QSysInfo::windowsVersion() >= QSysInfo::WV_WINDOWS10)
                    QDesktopServices::openUrl(QUrl(QStringLiteral("windowsdefender:")));
                else
                    windowsTool(QStringLiteral("control.exe"), QStringList() << QStringLiteral("wscui.cpl"));
#endif
            });
    auto clean = action(t("Disk Cleanup", "تنظيف القرص"), body);
    commands->addWidget(clean);
    connect(clean, &QPushButton::clicked, this, [this]() { windowsTool(QStringLiteral("cleanmgr.exe")); });
    auto remove = action(t("Uninstall programs", "إزالة البرامج"), body);
    commands->addWidget(remove);
    connect(remove, &QPushButton::clicked, this, [this]()
            { windowsTool(QStringLiteral("control.exe"), QStringList() << QStringLiteral("appwiz.cpl")); });
    for (auto button : {security, clean, remove})
    {
        button->setMinimumHeight(76);
        button->setIconSize(QSize(26, 26));
    }
    security->setText(t("Windows Security", "أمان ويندوز"));
    security->setIcon(VectorIcons::icon(QStringLiteral("shield"), Qt::white, 26));
    clean->setIcon(VectorIcons::icon(QStringLiteral("spark"), QColor(QStringLiteral("#00a7b5")), 26));
    remove->setIcon(VectorIcons::icon(QStringLiteral("apps"), QColor(QStringLiteral("#00a7b5")), 26));
    box->addLayout(commands);
    auto titleLine = new QHBoxLayout;
    titleLine->addWidget(
        copy(t("Software installed on the computer", "البرامج المثبتة على الكمبيوتر"), body, "section"), 1);
    auto suspect = new QCheckBox(t("Display suspicious software only", "عرض البرامج المشبوهة فقط"), body);
    suspect->setChecked(m_suspicious);
    connect(suspect, &QCheckBox::toggled, this,
            [this](bool v)
            {
                m_suspicious = v;
                expert(2);
            });
    titleLine->addWidget(suspect);
    box->addLayout(titleLine);
    box->addWidget(copy(t("This distribution has no local protection classifier data. Suspicious status is "
                          "unavailable; installed programs are not assumed safe or harmful. Windows Security "
                          "performs the actual security scan.",
                          "لا تحتوي هذه المكتبة بيانات تصنيف حماية محلية. حالة الاشتباه غير متاحة، ولا نعتبر "
                          "البرامج المثبتة آمنة أو ضارة تلقائيًا. فحص الحماية الفعلي من اختصاص أمان ويندوز."),
                        body, "caption"));
    if (!m_suspicious)
    {
        const auto installed = m_demo?QVector<DriverPackSoftware::Installed>{{QStringLiteral("7-Zip — sample"),QStringLiteral("Igor Pavlov"),QStringLiteral("Preview")}}:DriverPackSoftware::installedPrograms();
        auto tree = new ProjectTable(body);
        tree->setHeaderLabels(QStringList() << t("Program", "البرنامج") << t("Version", "الإصدار")
                                            << t("Publisher", "المطور"));
        tree->setRootIsDecorated(false);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        tree->setMinimumHeight(350);
        for (const auto &p : installed)
        {
            auto item = new QTreeWidgetItem(tree);
            item->setText(0, p.name);
            item->setText(1, p.version);
            item->setText(2, p.publisher);
        }
        box->addWidget(tree);
    }
    auto addSecurity = action(t("Add security software…", "إضافة برنامج حماية…"), body);
    addSecurity->setObjectName(QStringLiteral("addSecuritySoftware"));
    connect(addSecurity, &QPushButton::clicked, this, [this]() { addProgramDialog(true); });
    box->addWidget(addSecurity, 0, Qt::AlignTrailing);
    section(box, t("Important security software", "برامج الحماية المتاحة"));
    for (int i = 0; i < m_programs.size(); ++i)
        if (!m_programs[i].excluded && m_programs[i].category == QStringLiteral("Antivirus"))
            programRow(box, i);
    box->addStretch();
}
static QJsonObject galleryHardware() {
    QJsonArray sections;
    auto add=[&](const char *klass,const char *name,const char *ar,const QJsonObject &item){QJsonObject row;row.insert(QStringLiteral("class"),QString::fromLatin1(klass));row.insert(QStringLiteral("name"),QString::fromUtf8(name));row.insert(QStringLiteral("arabic"),QString::fromUtf8(ar));row.insert(QStringLiteral("queryStatus"),QStringLiteral("0x00000000"));row.insert(QStringLiteral("items"),QJsonArray()<<item);sections<<row;};
    add("Win32_ComputerSystem","Computer","الكمبيوتر",{{QStringLiteral("Name"),QStringLiteral("DEMO-PC")},{QStringLiteral("Manufacturer"),QStringLiteral("Sample manufacturer")},{QStringLiteral("Model"),QStringLiteral("Demo workstation")},{QStringLiteral("TotalPhysicalMemory"),QStringLiteral("16 GB")}});
    add("Win32_OperatingSystem","Operating system","نظام التشغيل",{{QStringLiteral("Caption"),QStringLiteral("Windows — sample data")},{QStringLiteral("OSArchitecture"),QStringLiteral("64-bit")},{QStringLiteral("Version"),QStringLiteral("Preview")}});
    add("Win32_BaseBoard","Motherboard","اللوحة الأم",{{QStringLiteral("Manufacturer"),QStringLiteral("Sample manufacturer")},{QStringLiteral("Product"),QStringLiteral("Demo motherboard")}});
    add("Win32_BIOS","BIOS","بيوس",{{QStringLiteral("Manufacturer"),QStringLiteral("Sample firmware")},{QStringLiteral("Version"),QStringLiteral("1.0 — preview")}});
    add("Win32_Processor","Processor","المعالج",{{QStringLiteral("Name"),QStringLiteral("Sample processor")},{QStringLiteral("NumberOfCores"),QStringLiteral("8")}});
    add("Win32_VideoController","Video cards","بطاقات العرض",{{QStringLiteral("Name"),QStringLiteral("Sample graphics adapter")},{QStringLiteral("DriverVersion"),QStringLiteral("1.0.0 — preview")},{QStringLiteral("CurrentHorizontalResolution"),QStringLiteral("1920")},{QStringLiteral("CurrentVerticalResolution"),QStringLiteral("1080")}});
    add("Win32_DesktopMonitor","Monitors","الشاشات",{{QStringLiteral("Name"),QStringLiteral("Sample display")},{QStringLiteral("MonitorManufacturer"),QStringLiteral("Demo")}});
    add("Win32_SoundDevice","Sound devices","أجهزة الصوت",{{QStringLiteral("Name"),QStringLiteral("Sample audio device")},{QStringLiteral("Status"),QStringLiteral("Preview")}});
    add("Win32_NetworkAdapter","Network adapters","بطاقات الشبكة",{{QStringLiteral("Name"),QStringLiteral("Sample network adapter")},{QStringLiteral("Manufacturer"),QStringLiteral("Demo")}});
    QJsonObject data;data.insert(QStringLiteral("sections"),sections);return data;
}
void DriverPackShell::refreshHardware()
{
    if(m_demo){m_hardwareData=galleryHardware();return;}
    if (m_hardware->isRunning())
        return;
    m_hardwareData = QJsonObject();
    m_hardware->stop.store(false);
    m_hardware->start(QThread::LowPriority);
}
void DriverPackShell::buildDiagnostics(QWidget *page)
{
    page->setObjectName(QStringLiteral("diagnosticsPage"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *box = new QVBoxLayout(page);
    box->setContentsMargins(28, 22, 28, 22);
    box->setSpacing(18);
    QWidget *body = page;
    auto hero = new AccentCard(page);
    hero->setStyleSheet(
        QStringLiteral("QLabel{color:white;} QPushButton{padding:0;border:0;background:transparent;}"));
    auto heroBox = new QHBoxLayout(hero);
    auto labels = new QVBoxLayout;
    labels->setSpacing(6);
    labels->addWidget(copy(t("Computer diagnostics", "تشخيص الكمبيوتر"), hero, "title"));
    labels->addWidget(copy(t("Explore your hardware, system records and Windows information.",
                             "استعرض مكونات جهازك وسجلات النظام ومعلومات ويندوز."),
                           hero, "caption"));
    heroBox->addLayout(labels, 1);
    heroBox->setContentsMargins(22, 18, 22, 20);
    auto title = new QHBoxLayout;

    auto refresh = action(t("Refresh", "تحديث"), hero);
    refresh->setEnabled(!m_hardware->isRunning());
    connect(refresh, &QPushButton::clicked, this,
            [this]()
            {
                refreshHardware();
                expert(3);
            });
    title->addWidget(refresh, 0, Qt::AlignVCenter);
    auto save = action(t("Save report…", "حفظ تقرير…"), hero);
    connect(save, &QPushButton::clicked, this,
            [this]()
            {
                const QString f = QFileDialog::getSaveFileName(
                    this, t("Save hardware report", "حفظ تقرير المكونات"),
                    QStringLiteral("DriverPack-hardware.json"), QStringLiteral("JSON (*.json)"));
                if (f.isEmpty())
                    return;
                QSaveFile out(f);
                const QByteArray data = QJsonDocument(m_hardwareData).toJson();
                if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size())
                    out.commit();
            });
    title->addWidget(save, 0, Qt::AlignVCenter);
    heroBox->addLayout(title);
    heroBox->setAlignment(title, Qt::AlignVCenter);
    box->addWidget(hero);
    if (m_hardwareData.isEmpty())
    {
        box->addWidget(copy(m_hardware->isRunning()
                                ? t("Reading system information…", "جارٍ قراءة معلومات النظام…")
                                : t("Read the component information reported by Windows.",
                                    "قراءة بيانات مكونات الجهاز كما يبلغ عنها ويندوز."),
                            body, "largeCaption"));
        if (!m_hardware->error.isEmpty())
            box->addWidget(copy(m_hardware->error, body, "caption"));
        else if (!m_hardware->isRunning() &&
                 m_settings->value(QStringLiteral("driverpack/enableDiagnostics"), true).toBool())
            QTimer::singleShot(0, this, [this]() { refreshHardware(); });
        box->addStretch();
        if (m_hardware->isRunning() ||
            (m_hardware->error.isEmpty() &&
             m_settings->value(QStringLiteral("driverpack/enableDiagnostics"), true).toBool()))
            new ContentLoader(
                page, t("Please wait", "يرجى الانتظار"),
                t("Reading your computer's hardware information…", "جارٍ قراءة معلومات مكونات جهازك…"));
        return;
    }
    auto propertyCaption = [this](const QString &field)
    {
        QString label = hardwarePropertyLabel(field, m_arabic);
        if (!m_arabic)
        {
            label.replace(QRegularExpression(QStringLiteral("([A-Z]+)([A-Z][a-z])")),
                          QStringLiteral("\\1 \\2"));
            label.replace(QRegularExpression(QStringLiteral("([a-z])([A-Z])")), QStringLiteral("\\1 \\2"));
            label = UiLanguage::text(label);
        }
        return label;
    };
    auto navigation = new SettingsTabs(page);
    navigation->setObjectName(QStringLiteral("diagnosticNavigation"));
    box->addWidget(navigation, 1);
    const char *en[] = {"Computer", "Processes and events", "Display", "Multimedia", "Software", "Security"};
    const char *ar[] = {"الكمبيوتر", "العمليات والأحداث", "العرض", "الوسائط المتعددة", "البرامج", "الحماية"};
    auto blockFor = [](const QString &klass)
    {
        if (klass == QStringLiteral("Win32_Process") || klass == QStringLiteral("Win32_Service"))
            return 1;
        if (klass == QStringLiteral("Win32_VideoController") ||
            klass == QStringLiteral("Win32_DesktopMonitor"))
            return 2;
        if (klass == QStringLiteral("Win32_SoundDevice"))
            return 3;
        return 0;
    };
    auto sections = m_hardwareData.value(QStringLiteral("sections")).toArray();
    QVector<QJsonValue> ordered;
    const QStringList priority = {QStringLiteral("Win32_ComputerSystem"),
                                  QStringLiteral("Win32_OperatingSystem"), QStringLiteral("Win32_BaseBoard"),
                                  QStringLiteral("Win32_BIOS"), QStringLiteral("Win32_Processor")};
    for (const auto &name : priority)
        for (const auto &section : sections)
            if (section.toObject().value(QStringLiteral("class")).toString() == name)
                ordered << section;
    for (const auto &section : sections)
        if (!priority.contains(section.toObject().value(QStringLiteral("class")).toString()))
            ordered << section;
    for (const int block : {0, 2, 3, 1, 4, 5})
    {
        QVector<QWidget *> recordCards;
        auto sectionPage = new QWidget(navigation);
        body = pageBody(sectionPage, &box);
        box->setContentsMargins(4, 8, 14, 18);
        box->setSpacing(18);
        auto heading = copy(t(en[block], ar[block]), body, "title");

        const char *blockIcons[] = {"computer", "settings", "monitor", "audio", "apps", "shield"};
        navigation->addTab(sectionPage,
                           VectorIcons::icon(QString::fromLatin1(blockIcons[block]), QColor("#00aaa9"), 22),
                           block == 1 ? t("Activity", "النشاط") : heading->text());
        navigation->setTabToolTip(navigation->count() - 1, heading->text());
        box->addSpacing(24);
        box->addWidget(heading);
        auto compact = new DiagnosticGrid(body);
        compact->setObjectName(QStringLiteral("diagnosticCardGrid"));
        box->addWidget(compact);
        for (const auto &value : ordered)
        {
            const auto data = value.toObject();
            const QString klass = data.value(QStringLiteral("class")).toString();
            if (blockFor(klass) != block)
                continue;
            if (klass == QStringLiteral("Win32_BaseBoard"))
            {
                compact = new DiagnosticGrid(body);
                compact->setObjectName(QStringLiteral("diagnosticCardGrid"));
                box->addWidget(compact);
            }
            const auto items = data.value(QStringLiteral("items")).toArray();
            QString caption =
                data.value(m_arabic ? QStringLiteral("arabic") : QStringLiteral("name")).toString();
            if (!m_arabic)
                caption = UiLanguage::text(caption);
            auto card = new SurfaceCard(body);
            card->setProperty("diagnosticClass", klass);
            card->setProperty("tone", block == 0   ? QStringLiteral("blue")
                                      : block == 1 ? QStringLiteral("violet")
                                                   : QStringLiteral("teal"));
            auto content = new QVBoxLayout(card);
            content->setContentsMargins(20, 18, 20, 22);
            content->setSpacing(16);
            auto header = new QHBoxLayout;
            auto icon = copy(QString(), card);
            QString symbol =
                klass.contains(QStringLiteral("Network")) ? QStringLiteral("network")
                : klass.contains(QStringLiteral("Disk"))  ? QStringLiteral("storage")
                : klass.contains(QStringLiteral("Video")) || klass.contains(QStringLiteral("Monitor"))
                    ? QStringLiteral("monitor")
                : klass.contains(QStringLiteral("Sound"))    ? QStringLiteral("audio")
                : klass.contains(QStringLiteral("USB"))      ? QStringLiteral("usb")
                : klass.contains(QStringLiteral("Computer")) ? QStringLiteral("computer")
                : klass.contains(QStringLiteral("Battery"))  ? QStringLiteral("battery")
                                                             : QStringLiteral("chip");
            icon->setPixmap(VectorIcons::icon(symbol, QColor(QStringLiteral("#12aab5")), 25).pixmap(25, 25));
            icon->setFixedSize(30, 30);
            header->addWidget(icon);
            auto title = copy(caption, card, "section");
            title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
            header->addWidget(title, 1);
            if (items.size() > 1)
                header->addWidget(copy(QString::number(items.size()), card, "caption"));
            content->addLayout(header);
            if (items.isEmpty())
            {
                const QString status = data.value(QStringLiteral("queryStatus")).toString();
                content->addWidget(copy(status != QStringLiteral("0x00000000")
                                            ? t("Windows could not report this section: %1",
                                                "تعذرت قراءة هذا القسم من ويندوز: %1")
                                                  .arg(status)
                                            : t("Not reported by Windows", "غير مُبلغ من ويندوز"),
                                        card, "caption"));
                compact->addCard(card);
                continue;
            }
            const bool list =
                (items.size() > 1) || klass == QStringLiteral("Win32_VideoController") ||
                klass == QStringLiteral("Win32_DesktopMonitor") ||
                klass.contains(QStringLiteral("Network")) || klass == QStringLiteral("Win32_Process") ||
                klass == QStringLiteral("Win32_Service") || klass == QStringLiteral("Win32_PhysicalMemory") ||
                klass == QStringLiteral("Win32_USBController") ||
                klass == QStringLiteral("Win32_IDEController") || klass == QStringLiteral("Win32_Keyboard") ||
                klass == QStringLiteral("Win32_PointingDevice");
            if (list)
            {
                QStringList fields;
                for (const auto &item : items)
                    for (const QString &field : item.toObject().keys())
                        if (!fields.contains(field))
                            fields << field;
                for (const QString &first : {QStringLiteral("Manufacturer"), QStringLiteral("Description"),
                                             QStringLiteral("Name"), QStringLiteral("DisplayName")})
                    if (fields.removeOne(first))
                        fields.prepend(first);
                auto table = new ProjectTable(card);
                table->setProperty("diagnosticTable", true);
                table->setObjectName(QStringLiteral("diagnosticRecordTable"));
                table->setRootIsDecorated(false);
                table->setAlternatingRowColors(false);
                table->setUniformRowHeights(true);
                table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
                QStringList labels;
                for (const QString &field : fields)
                    labels << propertyCaption(field);
                table->setHeaderLabels(labels);
                table->header()->setSectionResizeMode(QHeaderView::Interactive);
                table->header()->setDefaultSectionSize(145);
                table->header()->setSectionResizeMode(0, QHeaderView::Interactive);
                table->setColumnWidth(0, 300);
                const int rowHeight = qMax(42, table->fontMetrics().height() + 20);
                table->setFixedHeight(
                    qMin(390, table->header()->minimumHeight() + items.size() * rowHeight + 28));
                content->setAlignment(Qt::AlignTop);
                for (const auto &item : items)
                {
                    auto row = new QTreeWidgetItem(table);
                    const auto object = item.toObject();
                    for (int column = 0; column < fields.size(); ++column)
                    {
                        const QString text = object.value(fields[column]).toString();
                        row->setText(column, text);
                        row->setToolTip(column, text);
                        row->setSizeHint(column, QSize(0, rowHeight));
                    }
                }
                content->addWidget(table);
                if (block == 2)
                    box->addWidget(card);
                else
                    recordCards << card;
            }
            else
            {
                for (const auto &record : items)
                {
                    auto fields = new QGridLayout;
                    fields->setColumnStretch(0, 3);
                    fields->setColumnStretch(1, 7);
                    fields->setVerticalSpacing(12);
                    const auto object = record.toObject();
                    int row = 0;
                    for (auto it = object.begin(); it != object.end(); ++it)
                    {
                        auto label = copy(propertyCaption(it.key()), card, "caption");
                        fields->addWidget(label, row, 0);
                        QString text = it.value().toString();
                        if (text == QStringLiteral("True"))
                            text = t("Yes", "نعم");
                        if (text == QStringLiteral("False"))
                            text = t("No", "لا");
                        auto field = copy(ltr(text), card);
                        field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                        field->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                       Qt::TextSelectableByKeyboard);
                        field->setFocusPolicy(Qt::ClickFocus);
                        field->setToolTip(text);
                        fields->addWidget(field, row++, 1);
                    }
                    content->addLayout(fields);
                }
                content->addStretch();
                compact->addCard(card);
            }
        }
        if (!recordCards.isEmpty())
        {
            section(box, t("Component records", "سجلات المكونات"));
            for (auto card : recordCards)
                box->addWidget(card);
        }
        if (block == 1)
        {
            const auto log = m_hardwareData.value(QStringLiteral("eventLog")).toObject();
            section(box, t("Daily and weekly errors", "الأخطاء اليومية والأسبوعية"));
            if (log.contains(QStringLiteral("error")))
                box->addWidget(copy(t("System event log is unavailable. Windows error: %1",
                                      "سجل أحداث النظام غير متاح. خطأ ويندوز: %1")
                                        .arg(log.value(QStringLiteral("error")).toInt()),
                                    body, "caption"));
            else
            {
                box->addWidget(
                    copy(t("Last 24 hours: %1 · Last 7 days: %2", "آخر 24 ساعة: %1 · آخر 7 أيام: %2")
                             .arg(log.value(QStringLiteral("last24Hours")).toInt())
                             .arg(log.value(QStringLiteral("last7Days")).toInt()),
                         body));
                if (log.value(QStringLiteral("boundedRead")).toBool())
                    box->addWidget(copy(t("Counts cover the bounded portion read from the System log.",
                                          "الأعداد تخص الجزء المقروء ضمن حدود سجل النظام."),
                                        body, "caption"));
                auto table = new ProjectTable(body);
                table->setRootIsDecorated(false);
                table->setHeaderLabels(QStringList() << t("Time", "الوقت") << t("Source", "المصدر")
                                                     << t("Event ID", "رقم الحدث"));
                table->header()->setSectionResizeMode(1, QHeaderView::Stretch);
                for (const auto &error : log.value(QStringLiteral("errors")).toArray())
                {
                    const auto event = error.toObject();
                    auto row = new QTreeWidgetItem(table);
                    row->setText(0, event.value(QStringLiteral("time")).toString());
                    row->setText(1, event.value(QStringLiteral("source")).toString());
                    row->setText(2, QString::number(event.value(QStringLiteral("eventId")).toInt()));
                }
                table->setMinimumHeight(220);
                box->addWidget(table);
            }
        }
        if (block == 4)
        {
            section(box, t("Installed software", "البرامج المثبتة"));
            auto table = new ProjectTable(body);
            table->setRootIsDecorated(false);
            table->setHeaderLabels(QStringList() << t("Program", "البرنامج") << t("Version", "الإصدار")
                                                 << t("Publisher", "المطور"));
            table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
            for (const auto &installed : (m_demo?QVector<DriverPackSoftware::Installed>{{QStringLiteral("7-Zip — sample"),QStringLiteral("Igor Pavlov"),QStringLiteral("Preview")}}:DriverPackSoftware::installedPrograms()))
            {
                auto row = new QTreeWidgetItem(table);
                row->setText(0, installed.name);
                row->setText(1, installed.version);
                row->setText(2, installed.publisher);
            }
            table->setMinimumHeight(360);
            box->addWidget(table);
        }
        if (block == 5)
        {
            const auto security = m_hardwareData.value(QStringLiteral("security")).toObject();
            const auto defenderRows = security.value(QStringLiteral("defender"))
                                          .toObject()
                                          .value(QStringLiteral("items"))
                                          .toArray();
            const QJsonObject defender =
                defenderRows.isEmpty() ? QJsonObject() : defenderRows.at(0).toObject();
            auto status = [this](const QJsonObject &data, const QString &key)
            {
                return !data.value(key).isBool()  ? t("Unavailable", "غير متاح")
                       : data.value(key).toBool() ? t("Enabled", "مفعّل")
                                                  : t("Disabled", "معطّل");
            };
            auto card = [this, body, compact](const QString &title, const QString &value,
                                              const QString &detail, const QString &symbol)
            {
                auto face = new SurfaceCard(body);
                face->setObjectName(QStringLiteral("securityStatusCard"));
                face->setProperty("tone", QStringLiteral("teal"));
                auto layout = new QVBoxLayout(face);
                layout->setContentsMargins(22, 18, 22, 22);
                layout->setSpacing(12);
                auto heading = new QHBoxLayout;
                auto icon = copy(QString(), face);
                icon->setPixmap(VectorIcons::icon(symbol, QColor("#00aaa9"), 24).pixmap(24, 24));
                heading->addWidget(icon);
                heading->addWidget(copy(title, face, "heading"), 1);
                layout->addLayout(heading);
                auto state = copy(value, face, "heading");
                if (value == t("Enabled", "مفعّل"))
                    state->setStyleSheet(QStringLiteral("color:#00a99c;font-weight:600;"));
                else if (value == t("Disabled", "معطّل"))
                    state->setStyleSheet(QStringLiteral("color:#c18a26;font-weight:600;"));
                layout->addWidget(state);
                layout->addWidget(copy(detail, face, "caption"));
                compact->addCard(face);
            };
            const QString reported = t("Read from the local Windows provider", "من مزود ويندوز المحلي");
            card(t("Microsoft Defender Antivirus", "مكافحة الفيروسات من Microsoft Defender"),
                 status(defender, QStringLiteral("AntivirusEnabled")), reported, QStringLiteral("shield"));
            card(t("Real-time protection", "الحماية في الوقت الفعلي"),
                 status(defender, QStringLiteral("RealTimeProtectionEnabled")), reported,
                 QStringLiteral("shield"));
            auto date = [this](const QJsonValue &value)
            {
                const QString raw = value.toString();
                auto parsed = QDateTime::fromString(raw.left(14), QStringLiteral("yyyyMMddHHmmss"));
                bool validOffset = false;
                const int minutes = raw.mid(22, 3).toInt(&validOffset);
                if (raw.size() >= 25 && validOffset &&
                    (raw.at(21) == QLatin1Char('+') || raw.at(21) == QLatin1Char('-')))
                    parsed.setOffsetFromUtc(minutes * 60 * (raw.at(21) == QLatin1Char('-') ? -1 : 1));
                return parsed.isValid() && parsed.date().year() > 1900
                           ? parsed.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                           : t("Not recorded", "غير مسجّل");
            };
            card(t("Last signature update", "آخر تحديث لقاعدة الحماية"),
                 date(defender.value(QStringLiteral("AntivirusSignatureLastUpdated"))), reported,
                 QStringLiteral("refresh"));
            card(t("Last quick scan", "آخر فحص سريع"),
                 date(defender.value(QStringLiteral("QuickScanEndTime"))), reported,
                 QStringLiteral("search"));
            card(t("Windows Firewall", "جدار حماية ويندوز"),
                 t("Domain: %1 · Private: %2 · Public: %3", "النطاق: %1 · الخاص: %2 · العام: %3")
                     .arg(status(security, QStringLiteral("firewall1")),
                          status(security, QStringLiteral("firewall2")),
                          status(security, QStringLiteral("firewall4"))),
                 t("Reported separately for each network profile", "حالة كل ملف شبكة على حدة"),
                 QStringLiteral("shield"));
            card(t("UAC configuration", "إعداد التحكم في حساب المستخدم"),
                 status(security, QStringLiteral("uacConfigured")),
                 t("Configured Windows setting", "القيمة المضبوطة في إعدادات ويندوز"),
                 QStringLiteral("settings"));
            card(t("Secure Boot", "الإقلاع الآمن"), status(security, QStringLiteral("secureBoot")), reported,
                 QStringLiteral("shield"));
            QStringList products;
            for (const auto &item : security.value(QStringLiteral("antivirus"))
                                        .toObject()
                                        .value(QStringLiteral("items"))
                                        .toArray())
                products << item.toObject().value(QStringLiteral("displayName")).toString();
            card(t("Registered antivirus products", "برامج مكافحة الفيروسات المسجّلة"),
                 products.isEmpty() ? t("Unavailable", "غير متاح") : products.join(QStringLiteral(" · ")),
                 t("Registration does not certify current protection state",
                   "التسجيل لا يثبت حالة الحماية الحالية"),
                 QStringLiteral("apps"));
            auto actions = new QHBoxLayout;
            auto open = action(t("Windows Security", "أمان ويندوز"), body);
            connect(open, &QPushButton::clicked, this, [this]() { expert(2); });
            actions->addWidget(open);
            auto firewall = action(t("Firewall settings", "إعدادات الجدار الناري"), body);
            connect(firewall, &QPushButton::clicked, this,
                    [this]()
                    {
                        windowsTool(QStringLiteral("control.exe"), QStringList()
                                                                       << QStringLiteral("firewall.cpl"));
                    });
            actions->addWidget(firewall);
            actions->addStretch();
            box->addLayout(actions);
        }
    }
    navigation->setCurrentIndex(qBound(0, m_diagnosticTab, navigation->count() - 1));
    connect(navigation, &QTabWidget::currentChanged, this, [this](int index) { m_diagnosticTab = index; });
    if (m_hardware->isRunning())
        new ContentLoader(page, t("Refreshing diagnostics", "تحديث التشخيص"),
                          t("Please wait while the component information is refreshed.",
                            "يرجى الانتظار أثناء تحديث معلومات المكونات."));
    box->addWidget(copy(t("Read-only Windows status. Unavailable does not mean protection is disabled.",
                          "قراءة فقط لحالة ويندوز. غير متاح لا تعني أن الحماية معطّلة."),
                        body, "caption"));
}

namespace
{
class ChoiceTree : public ProjectTable
{
  public:
    explicit ChoiceTree(QWidget *p) : ProjectTable(p) { setSelectionMode(QAbstractItemView::NoSelection); }

  protected:
    void mousePressEvent(QMouseEvent *e) Q_DECL_OVERRIDE
    {
        auto item = itemAt(e->pos());
        if (e->button() == Qt::LeftButton && item && (item->flags() & Qt::ItemIsUserCheckable))
        {
            item->setCheckState(0, item->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            viewport()->update();
            e->accept();
            return;
        }
        QTreeWidget::mousePressEvent(e);
    }
};
} // namespace
class TransferProgress : public QProgressBar
{
    QTimer tick;
    qreal phase = 0;

  public:
    explicit TransferProgress(QWidget *parent) : QProgressBar(parent)
    {
        setMinimumHeight(28);
        tick.setInterval(40);
        connect(&tick, &QTimer::timeout, this,
                [this]()
                {
                    phase += .025;
                    if (phase > 1)
                        phase = 0;
                    update();
                });
    }

  protected:
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE
    {
        QProgressBar::showEvent(event);
        tick.start();
    }
    void hideEvent(QHideEvent *event) Q_DECL_OVERRIDE
    {
        tick.stop();
        QProgressBar::hideEvent(event);
    }
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = Theme::isDark(this), busy = maximum() == minimum();
        const QRectF face = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::color("elevated", dark));
        painter.drawRoundedRect(face, 14, 14);
        const qreal ratio =
            busy ? .3 : qBound(0., qreal(value() - minimum()) / qMax(1, maximum() - minimum()), 1.);
        QRectF fill = face;
        fill.setWidth(face.width() * ratio);
        if (busy)
            fill.moveLeft(face.left() + (face.width() - fill.width()) *
                                            (qApp->property("reduceMotion").toBool() ? .5 : phase));
        if (layoutDirection() == Qt::RightToLeft)
            fill.moveLeft(face.right() - (fill.right() - face.left()));
        QLinearGradient gradient(fill.topLeft(), fill.topRight());
        gradient.setColorAt(0, Theme::color("headerStart", dark));
        gradient.setColorAt(1, Theme::color("accent", dark));
        painter.setBrush(gradient);
        painter.drawRoundedRect(fill, 14, 14);
        if (!busy)
        {
            painter.setPen(Theme::color("text", dark));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("%1%").arg(qRound(ratio * 100)));
        }
    }
};
class DrpTransferDialog : public RoundedDialog
{
    OnlineWorker *worker;
    ChoiceTree *tree;
    QProgressBar *progress;
    QLabel *status, *summary, *downRate, *upRate, *receivedValue, *peerValue;
    QPushButton *download, *check, *import, *pause;
    bool pauseRequested = false, paused = false;
    QVector<QPushButton *> selectionButtons;
    OnlinePackages::Catalog catalog;
    QStringList needed;
    bool ar, builder = false, closing = false;
    QString tr(const char *en, const char *arabic) const
    {
        return UiLanguage::text(QString::fromUtf8(en), QString::fromUtf8(arabic));
    }
    void summarize()
    {
        QVector<int> selected;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->checkState(0) == Qt::Checked)
                selected << tree->topLevelItem(i)->data(0, Qt::UserRole).toInt();
        summary->setText(tr("Selected: %1 · Download size: %2", "المحدد: %1 · حجم التنزيل: %2")
                             .arg(selected.size())
                             .arg(bytes(OnlinePackages::requiredBytes(catalog, selected))));
        QScopedPointer<QSettings> prefs(createAppSettings(QString(), 0, false));
        download->setEnabled(!worker->isRunning() && !paused &&
                             (!selected.isEmpty() ||
                              (builder && prefs->value(QStringLiteral("offline/category0"), true).toBool())));
    }
    void loaded()
    {
        catalog = worker->catalog;
        const QSignalBlocker blockRows(tree);
        tree->clear();
        const auto available =
            OnlinePackages::availability(catalog, worker->drivers, worker->indexes, needed);
        QScopedPointer<QSettings> preferences(createAppSettings(QString(), 0, false));
        for (int i = 0; i < catalog.files.size(); ++i)
        {
            const auto f = catalog.files[i];
            if (f.kind == OnlinePackages::Other)
                continue;
            auto row = new QTreeWidgetItem(tree);
            row->setData(0, Qt::UserRole, i);
            row->setText(0, f.targetName);
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            row->setCheckState(0, (builder ? offlineCategoryEnabled(
                                                 *preferences, OnlinePackages::packageCategory(f.targetName))
                                           : available[i].needed)
                                      ? Qt::Checked
                                      : Qt::Unchecked);
            row->setText(1, bytes(f.size));
            row->setText(2, QStringLiteral("0%"));
            row->setText(3, available[i].needed ? tr("Yes", "نعم") : QStringLiteral("—"));
            row->setText(4, available[i].missing  ? tr("Not downloaded", "غير محمّل")
                            : available[i].update ? tr("Update", "تحديث")
                                                  : tr("Current", "حالي"));
        }
        status->setText(OnlinePackages::containsDatabase(catalog)
                            ? tr("Catalog ready: %1 packages and indexes. Choose what to download.",
                                 "الكتالوج جاهز: %1 حزمة وفهرس. اختر ما تريد تنزيله.")
                                  .arg(tree->topLevelItemCount())
                            : tr("This torrent has no standalone SQLite index. Archives alone do not refresh "
                                 "matching results.",
                                 "هذا التورنت لا يحتوي فهرس SQLite مستقلًا. تنزيل الأرشيفات وحده لا يحدّث "
                                 "نتائج المطابقة."));
        if (!parentWidget()->property("readOnlyPreview").toBool() && !catalog.metadata.isEmpty())
        {
            QDir().mkpath(worker->cache);
            QSaveFile out(QDir(worker->cache).filePath(QStringLiteral("catalog.torrent")));
            if (out.open(QIODevice::WriteOnly) && out.write(catalog.metadata) == catalog.metadata.size())
                out.commit();
        }
        summarize();
    }
    void start(bool metadata)
    {
        if (worker->isRunning())
            return;
        worker->checking = metadata;
        pauseRequested = false;
        paused = false;
        pause->setEnabled(!metadata);
        pause->setText(tr("Pause", "إيقاف مؤقت"));
        pause->setIcon(VectorIcons::icon(QStringLiteral("pause"), QColor("#00aaa9"), 18));
        pause->setToolTip(pause->text());
        for (auto button : selectionButtons)
            button->setEnabled(false);
        worker->error.clear();
        worker->stop.store(false);
        worker->catalog = catalog;
        if (!metadata)
        {
            worker->selected.clear();
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                if (tree->topLevelItem(i)->checkState(0) == Qt::Checked)
                    worker->selected << tree->topLevelItem(i)->data(0, Qt::UserRole).toInt();
            if (worker->selected.isEmpty())
            {
                QScopedPointer<QSettings> prefs(createAppSettings(QString(), 0, false));
                if (builder && prefs->value(QStringLiteral("offline/category0"), true).toBool())
                {
                    transferCompleted = true;
                    QDialog::accept();
                }
                return;
            }
        }
        progress->setRange(0, 0);
        status->setText(metadata ? tr("Checking the official catalog…", "جارٍ فحص الكتالوج الرسمي…")
                                 : tr("Starting the selected downloads…", "جارٍ بدء تنزيل الملفات المحددة…"));
        status->setProperty("transferState", QStringLiteral("checking"));
        status->style()->unpolish(status);
        status->style()->polish(status);
        check->setEnabled(false);
        import->setEnabled(false);
        download->setEnabled(false);
        tree->setEnabled(false);
        if (!parentWidget()->property("readOnlyPreview").toBool())
            worker->start();
    }

  public:
    bool changed = false, transferCompleted = false;
    DrpTransferDialog(const QString &library, const QStringList &packages, bool arabic, QWidget *parent,
                      bool configure = false)
        : RoundedDialog(parent), needed(packages), ar(arabic), builder(configure)
    {
        setWindowTitle(tr("Download DriverPack Offline", "تنزيل درايفر باك أوفلاين"));
        setLayoutDirection(ar ? Qt::RightToLeft : Qt::LeftToRight);
        resize(qMin(1080, parent->width() - 40), qMin(740, parent->height() - 40));
        worker = new OnlineWorker(this);
        worker->drivers = QDir(library).filePath(QStringLiteral("drivers"));
        worker->indexes = QDir(library).filePath(QStringLiteral("index"));
        worker->cache = QDir(worker->drivers).filePath(QStringLiteral(".driverpack-modern-downloads"));
        worker->metadataUrl = OnlinePackages::officialMetadataUrl(QStringLiteral("DriverPack"));
        auto frame = new QVBoxLayout(this);
        frame->setContentsMargins(0, 0, 0, 0);
        frame->setSpacing(0);
        frame->addWidget(new DialogHeader(windowTitle(), QStringLiteral("download"), this, ar));
        auto content = new QWidget(this);
        auto box = new QVBoxLayout(content);
        box->setContentsMargins(24, 18, 24, 22);
        frame->addWidget(content, 1);
        box->setSpacing(12);
        auto button = [this](const QString &label, const QString &icon)
        {
            auto result = new QPushButton(label, this);
            result->setProperty("projectAction", true);
            result->setObjectName(QStringLiteral("drpAction"));
            result->setMinimumHeight(40);
            result->setCursor(Qt::PointingHandCursor);
            result->setToolTip(label);
            result->setIcon(VectorIcons::icon(icon, QColor("#00aaa9"), 18));
            result->setIconSize(QSize(18, 18));
            return result;
        };
        auto commands = new QHBoxLayout;
        check = button(tr("Check for updates", "البحث عن تحديثات"), QStringLiteral("refresh"));
        check->setObjectName(QStringLiteral("checkTransferUpdates"));
        import = button(tr("Open torrent catalog…", "فتح كتالوج تورنت…"), QStringLiteral("folder"));
        commands->addWidget(check);
        commands->addWidget(import);
        commands->addStretch();
        box->addLayout(commands);
        tree = new ChoiceTree(this);
        tree->setHeaderLabels(QStringList()
                              << tr("Package", "الحزمة") << tr("Size", "الحجم") << tr("Progress", "التقدم")
                              << tr("For this PC", "لهذا الجهاز") << tr("Status", "الحالة"));
        tree->setRootIsDecorated(false);
        tree->header()->setSectionResizeMode(QHeaderView::Interactive);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        tree->setColumnWidth(1, 115);
        tree->setColumnWidth(2, 145);
        tree->setColumnWidth(3, 115);
        tree->setColumnWidth(4, 150);
        tree->setObjectName(QStringLiteral("transferRecords"));
        box->addWidget(tree, 1);
        auto selectors = new QHBoxLayout;
        const char *en[] = {"Select all", "This PC only", "Network only", "Clear selection"};
        const char *arab[] = {"تحديد الكل", "لهذا الجهاز فقط", "الشبكة فقط", "إلغاء التحديد"};
        for (int k = 0; k < 4; ++k)
        {
            auto b = button(tr(en[k], arab[k]), QStringLiteral("check"));
            selectors->addWidget(b);
            selectionButtons << b;
            connect(b, &QPushButton::clicked, this,
                    [this, k]()
                    {
                        for (int i = 0; i < tree->topLevelItemCount(); ++i)
                        {
                            auto item = tree->topLevelItem(i);
                            const auto f = catalog.files[item->data(0, Qt::UserRole).toInt()];
                            const QString type = OnlinePackages::packageCategory(f.targetName);
                            item->setCheckState(
                                0, k == 0 || (k == 1 && needed.contains(f.targetName)) ||
                                           (k == 2 && (type == QStringLiteral("network") ||
                                                       f.targetName.contains(QStringLiteral("LAN_")) ||
                                                       f.targetName.contains(QStringLiteral("WLAN"))))
                                       ? Qt::Checked
                                       : Qt::Unchecked);
                        }
                    });
        }
        box->addLayout(selectors);
        summary = new QLabel(this);
        box->addWidget(summary);
        auto transferCard = new SurfaceCard(content);
        auto transferLayout = new QVBoxLayout(transferCard);
        transferLayout->setContentsMargins(18, 14, 18, 18);
        transferLayout->setSpacing(10);
        box->addWidget(transferCard);
        progress = new TransferProgress(transferCard);
        progress->setRange(0, 1000);
        progress->setValue(0);
        transferLayout->addWidget(progress);
        auto metrics = new QHBoxLayout;
        metrics->setSpacing(10);
        const QString titles[] = {tr("Download speed", "سرعة التنزيل"), tr("Upload speed", "سرعة الرفع"),
                                  tr("Received", "المستلم"), tr("Peers", "النظراء")};
        const QString icons[] = {QStringLiteral("download"), QStringLiteral("upload"),
                                 QStringLiteral("layers"), QStringLiteral("network")};
        QLabel **values[] = {&downRate, &upRate, &receivedValue, &peerValue};
        for (int i = 0; i < 4; ++i)
        {
            auto card = new SurfaceCard(transferCard);
            auto layout = new QVBoxLayout(card);
            layout->setContentsMargins(13, 10, 13, 12);
            layout->setSpacing(5);
            auto title = new QLabel(titles[i], card);
            title->setStyleSheet(QStringLiteral("font-size:12px;color:%1;")
                                     .arg(Theme::color("muted", Theme::isDark(this)).name()));
            auto heading = new QHBoxLayout;
            auto icon = new QLabel(card);
            icon->setPixmap(VectorIcons::icon(icons[i], QColor("#00aaa9"), 18).pixmap(18, 18));
            heading->addWidget(icon);
            heading->addWidget(title, 1);
            layout->addLayout(heading);
            *values[i] = new QLabel(QString::fromUtf8("—"), card);
            (*values[i])->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600;"));
            layout->addWidget(*values[i]);
            metrics->addWidget(card, 1);
        }
        transferLayout->insertLayout(0, metrics);
        status = new QLabel(tr("Check the official catalog or open a local torrent file.",
                               "افحص الكتالوج الرسمي أو افتح ملف تورنت محليًا."),
                            this);
        status->setWordWrap(true);
        status->setObjectName(QStringLiteral("transferStatus"));
        status->setMinimumHeight(42);
        transferLayout->addWidget(status);
        status->setStyleSheet(
            QStringLiteral(
                "QLabel#transferStatus{padding:9px 12px;border-radius:10px;} "
                "QLabel#transferStatus[transferState=error]{color:%1;background:%2;border:1px solid %3;}")
                .arg(Theme::isDark(this) ? QStringLiteral("#ffd6d6") : QStringLiteral("#a72a39"),
                     Theme::isDark(this) ? QStringLiteral("#432e3a") : QStringLiteral("#fff0f2"),
                     Theme::isDark(this) ? QStringLiteral("#89515e") : QStringLiteral("#efbcc4")));
        auto footer = new QHBoxLayout;
        footer->addStretch();
        pause = button(tr("Pause", "إيقاف مؤقت"), QStringLiteral("pause"));
        pause->setObjectName(QStringLiteral("pauseTransfer"));
        pause->setMinimumWidth(160);
        pause->setEnabled(false);
        download = new QPushButton(tr("Download selected", "تنزيل المحدد"), this);
        download->setObjectName(QStringLiteral("drpPrimary"));
        download->setMinimumHeight(44);
        download->setCursor(Qt::PointingHandCursor);
        footer->addWidget(pause);
        footer->addWidget(download);
        box->addLayout(footer);
        download->setEnabled(false);
        connect(pause, &QPushButton::clicked, this,
                [this]()
                {
                    if (paused)
                    {
                        start(false);
                        return;
                    }
                    if (worker->isRunning() && !worker->checking)
                    {
                        pauseRequested = true;
                        worker->stop.store(true);
                        pause->setEnabled(false);
                        pause->setText(tr("Pausing…", "جارٍ الإيقاف…"));
                        status->setText(
                            tr("Finishing the current block safely…", "جارٍ إنهاء الجزء الحالي بأمان…"));
                    }
                });
        connect(check, &QPushButton::clicked, this,
                [this]()
                {
                    worker->imported.clear();
                    start(true);
                });
        connect(import, &QPushButton::clicked, this,
                [this]()
                {
                    QString f = QFileDialog::getOpenFileName(
                        this, tr("Open DriverPack torrent", "فتح تورنت درايفر باك"), QString(),
                        QStringLiteral("Torrent (*.torrent)"));
                    if (f.isEmpty())
                        return;
                    QFile input(f);
                    if (input.open(QIODevice::ReadOnly) && input.size() < 8 * 1024 * 1024)
                    {
                        worker->imported = input.readAll();
                        start(true);
                    }
                });
        connect(download, &QPushButton::clicked, this, [this]() { start(false); });
        connect(tree, &QTreeWidget::itemChanged, this, [this]() { summarize(); });
        connect(worker, &OnlineWorker::activity, this,
                [this](const QString &s, qint64 done, qint64 total, int published)
                {
                    status->setText(s);
                    if (published)
                        changed = true;
                    if (total)
                    {
                        progress->setRange(0, 1000);
                        progress->setValue(done * 1000 / total);
                    }
                });
        connect(worker, &OnlineWorker::transferStats, this,
                [this](qint64 received, qint64 rate, qint64 upload, int peers)
                {
                    downRate->setText(bytes(qMax<qint64>(0, rate)) + QStringLiteral("/s"));
                    upRate->setText(bytes(qMax<qint64>(0, upload)) + QStringLiteral("/s"));
                    receivedValue->setText(bytes(received));
                    peerValue->setText(QString::number(peers));
                });
        connect(worker, &OnlineWorker::fileProgress, this,
                [this](int index, qint64 done, qint64 total)
                {
                    for (int i = 0; i < tree->topLevelItemCount(); ++i)
                    {
                        auto item = tree->topLevelItem(i);
                        if (item->data(0, Qt::UserRole).toInt() == index)
                            item->setText(2, QStringLiteral("%1%").arg(total ? done * 100 / total : 0));
                    }
                });
        connect(
            worker, &QThread::finished, this,
            [this]()
            {
                check->setEnabled(true);
                import->setEnabled(true);
                tree->setEnabled(true);
                progress->setRange(0, 1000);
                paused = pauseRequested && !worker->error.isEmpty() && !worker->checking;
                pauseRequested = false;
                pause->setEnabled(paused);
                pause->setText(paused ? tr("Resume", "استكمال") : tr("Pause", "إيقاف مؤقت"));
                pause->setIcon(VectorIcons::icon(paused ? QStringLiteral("play") : QStringLiteral("pause"),
                                                 QColor("#00aaa9"), 18));
                pause->setToolTip(pause->text());
                for (auto button : selectionButtons)
                    button->setEnabled(!paused);
                check->setEnabled(!paused);
                import->setEnabled(!paused);
                tree->setEnabled(!paused);
                if (paused)
                    status->setText(tr("Download paused. Resume continues from verified blocks.",
                                       "التنزيل متوقف مؤقتًا. الاستكمال يستخدم الأجزاء المتحقق منها."));
                else if (worker->error.isEmpty())
                {
                    if (worker->checking)
                        loaded();
                    else
                    {
                        changed = !worker->published.isEmpty();
                        transferCompleted = true;
                        status->setText(tr("Transfer finished. Rescan after closing this window.",
                                           "انتهى النقل. أعد الفحص بعد إغلاق النافذة."));
                    }
                }
                else
                {
                    status->setText(
                        worker->checking
                            ? tr("The catalog could not be checked. %1\nTry again or open a local torrent "
                                 "catalog.",
                                 "تعذر فحص الكتالوج. %1\nأعد المحاولة أو افتح كتالوج تورنت محليًا.")
                                  .arg(worker->error)
                            : tr("The download could not be completed. %1\nVerified blocks were kept for "
                                 "retrying.",
                                 "تعذر إكمال التنزيل. %1\nتم الاحتفاظ بالأجزاء المتحقق منها لإعادة المحاولة.")
                                  .arg(worker->error));
                }
                status->setProperty("transferState", paused                    ? QStringLiteral("paused")
                                                     : worker->error.isEmpty() ? QStringLiteral("ready")
                                                                               : QStringLiteral("error"));
                status->style()->unpolish(status);
                status->style()->polish(status);
                summarize();
                if (closing)
                    QDialog::accept();
            });
        QString fixture = QString::fromUtf8(qgetenv("DRIVERPACK_PREVIEW_CATALOG"));
        if (fixture.isEmpty())
            fixture = QDir(worker->cache).filePath(QStringLiteral("catalog.torrent"));
        if (!fixture.isEmpty())
        {
            QFile f(fixture);
            if (f.open(QIODevice::ReadOnly))
            {
                try
                {
                    worker->catalog = OnlinePackages::parse(f.readAll());
                    loaded();
                }
                catch (...)
                {
                }
            }
        }
    }
    bool previewPauseCycle(const QString &folder)
    {
        if (!parentWidget()->property("readOnlyPreview").toBool())
            return false;
        worker->checking = false;
        pauseRequested = true;
        worker->stop.store(true);
        worker->error = QStringLiteral("Preview pause; no transfer was started.");
        QMetaObject::invokeMethod(worker, "finished", Qt::DirectConnection);
        if (!paused || !pause->isEnabled())
            return false;
        grab().save(QDir(folder).filePath(QStringLiteral("downloads-paused-preview.png")));
        pause->click();
        if (paused || worker->stop.load() || !pause->isEnabled())
            return false;
        grab().save(QDir(folder).filePath(QStringLiteral("downloads-resume-preview.png")));
        worker->error.clear();
        QMetaObject::invokeMethod(worker, "finished", Qt::DirectConnection);
        return true;
    }
    ~DrpTransferDialog()
    {
        worker->stop.store(true);
        worker->wait();
    }

    void reject() Q_DECL_OVERRIDE
    {
        if (worker->isRunning())
        {
            closing = true;
            worker->stop.store(true);
            status->setText(tr("Stopping the transfer…", "جارٍ إيقاف النقل…"));
            return;
        }
        QDialog::reject();
    }

  protected:
    void closeEvent(QCloseEvent *e) Q_DECL_OVERRIDE
    {
        if (worker->isRunning())
        {
            closing = true;
            worker->stop.store(true);
            status->setText(tr("Stopping the transfer…", "جارٍ إيقاف النقل…"));
            e->ignore();
            return;
        }
        e->accept();
    }
};
QStringList DriverPackShell::neededPackages() const
{
    QStringList packages;
    for (const auto &e : m_catalog->entries())
        for (const auto &c : e.candidates)
            if (c.engineToken && !c.packageSource.isEmpty())
                packages << QFileInfo(c.packageSource).fileName();
    packages.removeDuplicates();
    return packages;
}
void DriverPackShell::downloads(bool builder)
{
    if (busy())
        return;
    DrpTransferDialog dialog(m_library, neededPackages(), m_arabic, this, builder);
    dialog.exec();
    if (builder && dialog.transferCompleted &&
        m_settings->value(QStringLiteral("offline/category0"), true).toBool())
    {
        if (dialog.changed)
            m_catalog->invalidateNative();
        downloadSoftwareOnly();
        return;
    }
    if (dialog.changed)
    {
        m_wanted = m_remote;
        m_remote.clear();
        beginScan();
    }
}
void DriverPackShell::buildOffline(QWidget *page)
{
    QVBoxLayout *box;
    auto body = pageBody(page, &box);
    auto hero = new AccentCard(body);
    auto heroBox = new QVBoxLayout(hero);
    heroBox->setContentsMargins(26, 24, 26, 24);
    heroBox->setSpacing(14);
    hero->setStyleSheet(QStringLiteral("QLabel{color:white;} QLabel#caption{color:#c8ecec;}"));
    heroBox->addWidget(
        copy(t("Your DriverPack Offline build", "مكتبتك من درايفر باك أوفلاين"), hero, "title"));
    heroBox->addWidget(
        copy(t("Choose the driver categories to keep available without an Internet connection.",
               "اختر فئات التعريفات التي تريد إتاحتها دون اتصال بالإنترنت."),
             hero, "caption"));
    box->addWidget(hero);
    auto download = action(t("Download DriverPack Offline…", "تنزيل درايفر باك أوفلاين…"), body, true);
    download->setMaximumWidth(520);
    QString catalogPath = QString::fromUtf8(qgetenv("DRIVERPACK_PREVIEW_CATALOG"));
    if (catalogPath.isEmpty())
        catalogPath =
            QDir(m_library).filePath(QStringLiteral("drivers/.driverpack-modern-downloads/catalog.torrent"));
    OnlinePackages::Catalog available;
    QFile metadata(catalogPath);
    if (metadata.open(QIODevice::ReadOnly) && metadata.size() < 8 * 1024 * 1024)
        try
        {
            available = OnlinePackages::parse(metadata.readAll());
        }
        catch (...)
        {
        }
    auto total = [this, download, available]()
    {
        qint64 amount = 0;
        for (const auto &file : available.files)
            if (file.kind != OnlinePackages::Other &&
                offlineCategoryEnabled(*m_settings, OnlinePackages::packageCategory(file.targetName)))
                amount += file.size;
        download->setText(
            amount > 0
                ? t("Download DriverPack Offline — %1", "تنزيل درايفر باك أوفلاين — %1").arg(bytes(amount))
                : t("Download DriverPack Offline…", "تنزيل درايفر باك أوفلاين…"));
    };
    total();
    connect(download, &QPushButton::clicked, this, [this]() { downloads(true); });
    box->addWidget(download, 0, Qt::AlignLeading);
    section(box, t("Download parameters", "خيارات التنزيل"));
    auto categoryCard = new SurfaceCard(body);
    categoryCard->setProperty("tone", QStringLiteral("blue"));
    auto categories = new QGridLayout(categoryCard);
    categories->setContentsMargins(24, 22, 24, 22);
    categories->setHorizontalSpacing(30);
    categories->setVerticalSpacing(24);
    const char *en[] = {"Programs",     "Chipset",      "Monitor",     "TV-Tuner",     "Video card",
                        "Network card", "Printer",      "Webcam",      "Other device", "Controller",
                        "Sound card",   "Wi-Fi device", "Card reader", "Modem",        "Input device"};
    const char *ar[] = {"البرامج",     "الشيبست",   "الشاشة",        "مستقبل التلفاز", "كارت الشاشة",
                        "كارت الشبكة", "الطابعة",   "الكاميرا",      "أجهزة أخرى",     "وحدات التحكم",
                        "كارت الصوت",  "الواي فاي", "قارئ البطاقات", "المودم",         "أجهزة الإدخال"};
    for (int i = 0; i < 15; ++i)
    {
        auto c = new QCheckBox(t(en[i], ar[i]), body);
        const QString key = QStringLiteral("offline/category%1").arg(i);
        c->setChecked(m_settings->value(key, true).toBool());
        connect(c, &QCheckBox::toggled, this,
                [this, key, total](bool value)
                {
                    m_settings->setValue(key, value);
                    total();
                });
        categories->addWidget(c, i % 4, i / 4);
    }
    box->addWidget(categoryCard);
    auto commands = new QHBoxLayout;
    auto folder = action(t("Open the Download folder", "فتح مجلد التنزيل"), body);
    connect(folder, &QPushButton::clicked, this,
            [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_library)); });
    commands->addWidget(folder);
    auto library = action(t("Choose library…", "اختيار مكتبة…"), body);
    connect(library, &QPushButton::clicked, this, [this]() { chooseLibrary(); });
    commands->addWidget(library);
    auto portable = action(t("Copy this offline build to USB…", "نسخ المكتبة الأوفلاين إلى USB…"), body);
    connect(portable, &QPushButton::clicked, this,
            [this]()
            {
                openPortableLibrary(this, QDir(m_library).filePath(QStringLiteral("drivers")),
                                    QDir(m_library).filePath(QStringLiteral("index")), neededPackages(),
                                    m_arabic);
            });
    commands->addWidget(portable);
    box->addLayout(commands);
    section(box, t("Local library", "المكتبة المحلية"));
    box->addWidget(copy(m_library, body));
    const QFileInfo db(QDir(m_library).filePath(QStringLiteral("index/db.sqlite")));
    box->addWidget(copy(
        db.exists() ? t("DriverPack SQLite index · %1", "فهرس SQLite لدرايفر باك · %1").arg(bytes(db.size()))
                    : t("No compatible SQLite index selected.", "لم يُحدد فهرس SQLite متوافق."),
        body, "caption"));
    box->addWidget(copy(t("The catalog may contain archives without a matching standalone database. A "
                          "library becomes ready only after its compatible index is available.",
                          "قد يحتوي كتالوج التنزيل أرشيفات دون قاعدة بيانات مستقلة مطابقة. تصبح المكتبة "
                          "جاهزة بعد توفر فهرسها المتوافق."),
                        body, "caption"));
    box->addStretch();
}
void DriverPackShell::setting(QVBoxLayout *box, const char *en, const char *ar, const char *captionEn,
                              const char *captionAr, const QString &key, bool initial)
{
    auto row = new SurfaceCard(box->parentWidget());
    row->setProperty("tone", QStringLiteral("teal"));
    auto layout = new QHBoxLayout(row);
    layout->setContentsMargins(22, 20, 22, 24);
    auto labels = new QVBoxLayout;
    labels->addWidget(copy(t(en, ar), row, "settingTitle"));
    labels->addWidget(copy(t(captionEn, captionAr), row, "caption"));
    layout->addLayout(labels, 1);
    auto toggle = new DrpSwitch(QString(), row);
    toggle->setObjectName(key);
    toggle->setChecked(m_settings->value(key, initial).toBool());
    toggle->setEnabled(true);
    toggle->setToolTip(t(captionEn, captionAr));
    toggle->setAccessibleName(t(en, ar));
    connect(toggle, &QCheckBox::toggled, this,
            [this, key, toggle](bool value)
            {
                m_settings->setValue(key, value);
                m_settings->sync();
                if (key == QStringLiteral("driverpack/compactMenu") ||
                    key == QStringLiteral("driverpack/showProtection") ||
                    key == QStringLiteral("driverpack/enableDiagnostics"))
                    QTimer::singleShot(180, toggle,
                                       [this]()
                                       {
                                           if (m_page != 5)
                                               return;
                                           rebuild();
                                           expert(5);
                                       });
                if (key == QStringLiteral("driverpack/showProtection") && m_nav.contains(2))
                    m_nav[2]->setVisible(value);
                if (key == QStringLiteral("driverpack/enableDiagnostics") && m_nav.contains(3))
                    m_nav[3]->setVisible(value);
            });
    layout->addWidget(toggle);
    if (m_settingsGrid)
        static_cast<DiagnosticGrid *>(m_settingsGrid)->addCard(row);
    else
        box->addWidget(row);
}
void DriverPackShell::buildSettings(QWidget *page)
{
    m_settingsGrid = 0;
    QVBoxLayout *box = new QVBoxLayout(page);
    box->setContentsMargins(28, 22, 28, 22);
    box->setSpacing(16);
    QWidget *body = page;
    page->setObjectName(QStringLiteral("settingsPage"));
    auto hero = new AccentCard(page);
    hero->setStyleSheet(
        QStringLiteral("QLabel{color:white;} QPushButton{padding:0;border:0;background:transparent;}"));
    auto heroBox = new QHBoxLayout(hero);
    auto labels = new QVBoxLayout;
    labels->setSpacing(6);
    labels->addWidget(copy(t("Settings", "الإعدادات"), hero, "title"));
    labels->addWidget(copy(
        t("Make DriverPack Modern work the way you prefer.", "اضبط درايفر باك مودرن بالطريقة المناسبة لك."),
        hero, "caption"));
    heroBox->addLayout(labels, 1);
    heroBox->setContentsMargins(22, 18, 22, 20);
    auto heading = new QHBoxLayout;

    auto report = action(t("Report a problem", "الإبلاغ عن مشكلة"), hero);
    report->setIcon(VectorIcons::icon(QStringLiteral("bug"), Qt::white, 22));
    connect(report, &QPushButton::clicked, this, [this]() { reportDialog(); });
    heading->addWidget(report);
    heroBox->addLayout(heading);
    heroBox->setAlignment(heading, Qt::AlignVCenter);
    box->addWidget(hero);
    auto tabs = new SettingsTabs(page);
    tabs->setObjectName(QStringLiteral("settingsTabs"));
    tabs->setDocumentMode(true);
    tabs->tabBar()->setExpanding(true);
    tabs->setElideMode(Qt::ElideRight);
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    box->addWidget(tabs, 1);
    auto category = [&](const QString &name, const QString &symbol)
    {
        if (body != page)
            box->addStretch();
        auto tab = new QWidget(tabs);
        tabs->addTab(tab, VectorIcons::icon(symbol, QColor("#00aaa9"), 20), name);
        body = pageBody(tab, &box);
        box->setContentsMargins(4, 18, 12, 16);
        m_settingsGrid = 0;
    };
    category(t("General", "عام"), QStringLiteral("settings"));
    section(box, t("General settings", "الإعدادات العامة"));
    auto language = new SurfaceCard(body);
    auto langLayout = new QHBoxLayout(language);
    langLayout->setContentsMargins(22, 20, 22, 24);
    auto langLabels = new QVBoxLayout;
    langLabels->addWidget(copy(t("Application language", "لغة البرنامج"), language, "settingTitle"));
    langLabels->addWidget(copy(t("Choose the language used in every screen.", "اختر لغة كل شاشات البرنامج."),
                               language, "caption"));
    langLayout->addLayout(langLabels, 1);
    auto lang = new SearchableLanguageCombo(language);
    lang->setObjectName(QStringLiteral("applicationLanguage"));
    lang->setProperty("languageSelector", true);
    lang->setMinimumWidth(260);
    lang->setToolTip(t("Choose application language", "اختيار لغة البرنامج"));
    for (const auto &locale : UiLanguage::available())
        lang->addItem(locale.second, locale.first);
    lang->setCurrentIndex(qMax(0, lang->findData(m_language)));
    connect(lang, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this,
            [this, lang](int i)
            {
                const QString locale = lang->itemData(i).toString();
                QTimer::singleShot(0, this, [this, locale]() { applyLanguage(locale); });
            });
    langLayout->addWidget(lang);
    box->addWidget(language);
    auto settingsGroup = [this, &body, &box]()
    {
        auto grid = new DiagnosticGrid(body);
        grid->setProperty("settingsGrid", true);
        m_settingsGrid = grid;
        box->addWidget(grid);
    };
    auto viewLog=action(t("View activity log…","عرض سجل النشاط…"),body);
    viewLog->setObjectName(QStringLiteral("viewActivityLog"));viewLog->setIcon(VectorIcons::icon(QStringLiteral("activity"),QColor("#00aaa9"),20));
    connect(viewLog,&QPushButton::clicked,this,[this](){logDialog();});box->addWidget(viewLog,0,Qt::AlignLeading);
    settingsGroup();
    setting(box, "Enable logging", "حفظ سجلات العمل",
            "Keep local logs for diagnosing scan or installation errors.",
            "حفظ سجلات محلية لتشخيص أخطاء الفحص أو التثبيت.", QStringLiteral("driverpack/enableLogging"),
            true);
    setting(box, "Remove installation temporary files", "حذف الملفات المؤقتة للتثبيت",
            "Remove extracted driver files after an installation finishes.",
            "حذف ملفات التعريف المستخرجة بعد انتهاء التثبيت.", QStringLiteral("driverpack/removeTemp"),
            false);
    setting(box, "Software and driver utilities", "البرامج وأدوات التعريفات",
            "Show optional driver toolkits in the driver installation list.",
            "إظهار أدوات التعريفات الاختيارية ضمن قائمة تثبيت التعريفات.",
            QStringLiteral("driverpack/showSoftware"), true);
    setting(box, "Protection and cleanup", "الحماية والتنظيف",
            "Show the protection and installed-software page.", "إظهار صفحة الحماية والبرامج المثبتة.",
            QStringLiteral("driverpack/showProtection"), true);
    setting(box, "Computer diagnostics", "تشخيص الكمبيوتر",
            "Read and display component information from Windows locally.",
            "قراءة بيانات المكونات محليًا من ويندوز وعرضها.", QStringLiteral("driverpack/enableDiagnostics"),
            true);
    setting(box, "Start in expert mode", "البدء في وضع الخبير",
            "Open the Drivers page instead of the automatic configuring screen.",
            "فتح صفحة التعريفات بدل شاشة الإعداد التلقائي عند بدء البرنامج.",
            QStringLiteral("driverpack/startExpert"), false);
    setting(box, "Compact menu", "القائمة المصغرة", "Use an icon-only side menu.",
            "استخدام قائمة جانبية تحتوي أيقونات فقط.", QStringLiteral("driverpack/compactMenu"), false);
    setting(box, "Select drivers by default", "تحديد التعريفات افتراضيًا",
            "Select recommended local packages after a successful scan.",
            "تحديد الحزم المحلية الموصى بها بعد نجاح الفحص.",
            QStringLiteral("driverpack/defaultRecommendations"), false);
    category(t("Recovery", "الاستعادة"), QStringLiteral("shield"));
    section(box, t("Errors and recovery", "الأخطاء والاستعادة"));
    settingsGroup();
    setting(box, "Create a restore point", "إنشاء نقطة استعادة",
            "Create a Windows restore point before changing drivers; failure stops installation.",
            "إنشاء نقطة استعادة لويندوز قبل تغيير التعريفات؛ فشلها يوقف التثبيت.",
            QStringLiteral("installation/restorePointRequired"), true);
    setting(box, "Check system conditions", "فحص ظروف النظام",
            "Verify administrator access and available temporary storage before installation.",
            "التحقق من صلاحيات المسؤول ومساحة التخزين المؤقتة قبل التثبيت.",
            QStringLiteral("driverpack/systemCheck"), true);
    setting(box, "Offer restart when required", "عرض إعادة التشغيل عند الحاجة",
            "Show the restart action on the result page when Windows requires a restart.",
            "إظهار زر إعادة التشغيل في صفحة النتيجة عندما يطلب ويندوز ذلك.",
            QStringLiteral("installation/rebootAfter"), false);
    auto backup = action(t("Create a driver backup…", "إنشاء نسخة احتياطية للتعريفات…"), body);
    connect(backup, &QPushButton::clicked, this,
            [this]()
            {
                if (m_capture || busy())
                    return;
                const QString folder = QFileDialog::getExistingDirectory(
                    this, t("Driver backup folder", "مجلد النسخة الاحتياطية"));
                if (!folder.isEmpty())
                    m_engine->requestBackup(folder);
            });
    box->addWidget(backup, 0, Qt::AlignLeading);
    category(t("Matching", "المطابقة"), QStringLiteral("chip"));
    section(box, t("Driver selection algorithm", "خوارزمية اختيار التعريفات"));
    settingsGroup();
    box->addWidget(copy(t("Matching uses this library's hardware IDs, Windows version and architecture, INF "
                          "sections and DriverPack rank. Online tracking and cloud machine-learning requests "
                          "are removed. Unsupported original rules are reported rather than ignored.",
                          "تعتمد المطابقة على معرّفات الأجهزة وإصدار ويندوز والمعمارية وأقسام INF وترتيب "
                          "درايفر باك في المكتبة. أزيلت طلبات التتبع والتعلم السحابي. القواعد الأصلية غير "
                          "المدعومة يُوضّح سببها بدل تجاهلها."),
                        body, "caption"));
    auto library = action(t("Library and matching source…", "المكتبة ومصدر المطابقة…"), body);
    connect(library, &QPushButton::clicked, this, [this]() { expert(4); });
    box->addWidget(library, 0, Qt::AlignLeading);
    category(t("Appearance", "المظهر"), QStringLiteral("layers"));
    section(box, t("Your additions · Appearance and themes", "إضافاتك · المظهر والثيمات"));
    auto appearanceCard = new SurfaceCard(body);
    auto appearanceBox = new QVBoxLayout(appearanceCard);
    appearanceBox->setContentsMargins(22, 22, 22, 24);
    appearanceBox->setSpacing(18);
    auto appearanceRow = new QHBoxLayout;
    appearanceRow->addWidget(copy(t("Appearance", "المظهر"), body, "settingTitle"), 1);
    auto theme = new SearchableLanguageCombo(body);
    theme->setMinimumWidth(300);
    theme->setObjectName(QStringLiteral("appearanceSelector"));
    theme->addItem(t("Light", "فاتح"), QStringLiteral("light"));
    theme->addItem(t("Dark", "داكن"), QStringLiteral("dark"));
    theme->addItem(t("Follow Windows", "اتباع ويندوز"), QStringLiteral("system"));
    theme->setCurrentIndex(qMax(0, theme->findData(m_themeMode)));
    connect(theme, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this,
            [this, theme](int i) { setDark(theme->itemData(i).toString()); });
    appearanceRow->addWidget(theme);
    appearanceBox->addLayout(appearanceRow);
    box->addWidget(appearanceCard);
    auto packs = new QHBoxLayout;
    auto import = action(t("Import theme…", "استيراد ثيم…"), body);
    connect(import, &QPushButton::clicked, this,
            [this]()
            {
                const QString file = QFileDialog::getOpenFileName(
                    this, t("Import theme", "استيراد ثيم"), QString(), QStringLiteral("Theme (*.json)"));
                if (file.isEmpty())
                    return;
                try
                {
                    Theme::setDocument(Theme::readPack(file));
                    m_settings->setValue(QStringLiteral("theme/nativePack"), file);
                    appearance();
                }
                catch (const std::exception &e)
                {
                    showMessage(t("Theme", "الثيم"), QString::fromUtf8(e.what()));
                }
            });
    packs->addWidget(import);
    auto exportTheme = action(t("Export theme…", "تصدير الثيم…"), body);
    connect(exportTheme, &QPushButton::clicked, this,
            [this]()
            {
                const QString file = QFileDialog::getSaveFileName(this, t("Export theme", "تصدير الثيم"),
                                                                  QStringLiteral("DriverPack.dpm-theme.json"),
                                                                  QStringLiteral("Theme (*.json)"));
                if (!file.isEmpty())
                    try
                    {
                        Theme::writePack(file, Theme::document());
                    }
                    catch (const std::exception &e)
                    {
                        showMessage(t("Theme", "الثيم"), QString::fromUtf8(e.what()));
                    }
            });
    packs->addWidget(exportTheme);
    appearanceBox->addLayout(packs);
    auto reset = action(t("Restore default settings", "استعادة الإعدادات الافتراضية"), body);
    connect(reset, &QPushButton::clicked, this, [this]() { restoreDefaults(); });
    box->addWidget(reset, 0, Qt::AlignLeading);
    box->addWidget(copy(t("DriverPack data and original authors retain their rights. Native interface and "
                          "adaptation: Tarek Sadek (Diagoo1).",
                          "حقوق بيانات درايفر باك ومؤلفيه محفوظة. الواجهة الأصلية لويندوز وإعادة البناء: "
                          "طارق صادق (Diagoo1)."),
                        body, "caption"));
    box->addStretch();
    tabs->setCurrentIndex(qBound(0, m_settingsTab, tabs->count() - 1));
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) { m_settingsTab = index; });
}
void DriverPackShell::buildAbout(QWidget *page)
{
    auto box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 0);
    auto about = new AboutPage(m_arabic, page);
    about->setProperty("darkMode", m_dark);
    about->setStyleSheet(Theme::styleSheet(m_dark));
    box->addWidget(about);
}
void DriverPackShell::setLanguage(bool ar)
{
    applyLanguage(ar ? QStringLiteral("arabic") : QStringLiteral("english"));
}
void DriverPackShell::applyLanguage(const QString &locale)
{
    if (busy())
        return;
    bool valid = false;
    for (const auto &item : UiLanguage::available())
        if (item.first == locale)
            valid = true;
    if (!valid)
        return;
    m_language = locale;
    m_arabic = locale == QStringLiteral("arabic");
    m_settings->setValue(QStringLiteral("language"), locale);
    m_settings->sync();
    UiLanguage::setLanguage(locale);
    UiFont::applyLanguage(locale);
    const bool wasStart = m_root->currentWidget() == m_start;
    rebuild();
    if (wasStart)
        m_root->setCurrentWidget(m_start);
    else
        expert(m_page);
}
void DriverPackShell::setDark(const QString &mode)
{
    m_themeMode = mode;
    m_settings->setValue(QStringLiteral("theme/mode"), mode);
    appearance();
    if (m_root->currentWidget() == m_expert)
        expert(m_page);
}
void DriverPackShell::restoreDefaults()
{
    if (!confirm(t("Restore defaults", "استعادة الافتراضيات"),
                 t("Restore application preferences? Your library, drivers and programs will not be deleted.",
                   "استعادة تفضيلات البرنامج؟ لن تُحذف المكتبة أو التعريفات أو البرامج.")))
        return;
    for (const QString &key : m_settings->allKeys())
        if (key.startsWith(QStringLiteral("driverpack/")) ||
            key.startsWith(QStringLiteral("installation/")) || key.startsWith(QStringLiteral("offline/")))
            m_settings->remove(key);
    m_settings->setValue(QStringLiteral("driverpack/library"), m_library);
    m_showInstalled = true;
    m_additional = false;
    rebuild();
    expert(5);
}
void DriverPackShell::windowsTool(const QString &file, const QStringList &args)
{
    if (m_capture || m_demo)
        return;
#ifdef Q_OS_WIN
    WCHAR system[MAX_PATH] = {};
    if (GetSystemDirectoryW(system, MAX_PATH))
        QProcess::startDetached(QDir(QString::fromWCharArray(system)).filePath(file), args);
#else
    Q_UNUSED(file);
    Q_UNUSED(args);
#endif
}
void DriverPackShell::chooseLibrary()
{
    if (busy())
        return;
    const QString root = QFileDialog::getExistingDirectory(
        this, t("Choose DriverPack library", "اختر مكتبة درايفر باك"), m_library);
    if (root.isEmpty())
        return;
    QString candidate = root;
    if (QFileInfo(root).fileName().compare(QStringLiteral("drivers"), Qt::CaseInsensitive) == 0 ||
        QFileInfo(root).fileName().compare(QStringLiteral("index"), Qt::CaseInsensitive) == 0)
        candidate = QFileInfo(root).absolutePath();
    if (!QFileInfo(QDir(candidate).filePath(QStringLiteral("index/db.sqlite"))).isFile())
    {
        showMessage(
            t("DriverPack library", "مكتبة درايفر باك"),
            t("The folder must contain index/db.sqlite and drivers. SDI/SDIO .bin indexes belong to a "
              "different project.",
              "يجب أن يحتوي المجلد index/db.sqlite وdrivers. فهارس SDI/SDIO ذات صيغة bin تخص مشروعًا آخر."));
        return;
    }
    m_library = candidate;
    m_settings->setValue(QStringLiteral("driverpack/library"), candidate);
    m_programSelection.clear();
    m_remote.clear();
    loadSoftware();
    beginScan();
}
void DriverPackShell::tools()
{
    if (!m_tools || busy())
        return;
    m_tools->setGeometry(0, m_header->height(), width(), height() - m_header->height());
    m_tools->setActionAvailable(QStringLiteral("extract"), m_catalog->selectedDrivers() > 0,
                                t("Select local drivers first.", "حدد تعريفات محلية أولًا."));
    for (auto row : m_tools->findChildren<QToolButton *>())
        if (row->property("toolId").toString() == QStringLiteral("snapshot"))
            row->setText(t("Save device report…", "حفظ تقرير الجهاز…"));
    m_tools->toggle();
}
void DriverPackShell::logDialog(){ActivityLog::show(this,m_library,m_arabic,m_demo);}
void DriverPackShell::toolAction(const QString &id)
{
    if(id==QStringLiteral("activity-log")){logDialog();return;}
    if (id == QStringLiteral("offline"))
    {
        expert(4);
        return;
    }
    if (id == QStringLiteral("settings"))
    {
        expert(5);
        return;
    }
    if (id == QStringLiteral("updates"))
    {
        downloads();
        return;
    }
    if (id == QStringLiteral("report-bug"))
    {
        reportDialog();
        return;
    }
    if (id == QStringLiteral("license"))
    {
        showMessage(t("License Agreement", "اتفاقية الترخيص"),
                    t("DriverPack Modern is GPL-3.0-or-later. DriverPack and the original publishers retain "
                      "their rights. See the included source notices.",
                      "درايفر باك مودرن مرخص وفق GPL-3.0-or-later. حقوق درايفر باك والناشرين الأصليين "
                      "محفوظة. راجع ملفات الحقوق بالمصدر."));
        return;
    }
    if (m_capture || m_demo || busy())
        return;
    if (id == QStringLiteral("restore"))
    {
        if (confirm(t("System restore", "استعادة النظام"),
                    t("Create a Windows restore point now? Administrator rights are required.",
                      "إنشاء نقطة استعادة لويندوز الآن؟ تحتاج صلاحيات المسؤول.")))
            m_engine->requestRestorePoint();
        return;
    }
    if (id == QStringLiteral("backup"))
    {
        const QString folder =
            QFileDialog::getExistingDirectory(this, t("Backup destination", "وجهة النسخة الاحتياطية"));
        if (!folder.isEmpty())
            m_engine->requestBackup(folder);
        return;
    }
    if (id == QStringLiteral("extract"))
    {
        const QString folder =
            QFileDialog::getExistingDirectory(this, t("Extraction destination", "وجهة الاستخراج"));
        if (!folder.isEmpty())
            m_engine->requestExtraction(m_catalog->buildPlan(false, true, true), folder);
        return;
    }
    if (id == QStringLiteral("snapshot"))
    {
        const QString file = QFileDialog::getSaveFileName(this, t("Save device report", "حفظ تقرير الجهاز"),
                                                          QStringLiteral("DriverPack-device-report.json"),
                                                          QStringLiteral("JSON (*.json)"));
        if (!file.isEmpty())
            m_engine->requestSnapshot(file);
        return;
    }
    if (id == QStringLiteral("automation") || id == QStringLiteral("report"))
    {
        QJsonObject document;
        try
        {
            document = id == QStringLiteral("automation")
                           ? automationPlan(
                                 *m_catalog,
                                 m_settings->value(QStringLiteral("installation/restorePointRequired"), true)
                                     .toBool())
                           : supportReport(*m_catalog, sdiomodern::InstallReport(), m_error);
        }
        catch (const std::exception &error)
        {
            showMessage(t("Export report", "تصدير تقرير"), QString::fromUtf8(error.what()));
            return;
        }
        const QString file = QFileDialog::getSaveFileName(
            this, t("Save JSON report", "حفظ تقرير JSON"),
            QStringLiteral("DriverPack-") + id + QStringLiteral(".json"), QStringLiteral("JSON (*.json)"));
        if (file.isEmpty())
            return;
        QSaveFile out(file);
        const QByteArray data = QJsonDocument(document).toJson();
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
            showMessage(t("Save failed", "فشل الحفظ"), out.errorString());
        return;
    }
    const QMap<QString, QStringList> commands = {
        {QStringLiteral("uninstall"), {QStringLiteral("control.exe"), QStringLiteral("appwiz.cpl")}},
        {QStringLiteral("device-manager"), {QStringLiteral("mmc.exe"), QStringLiteral("devmgmt.msc")}},
        {QStringLiteral("system-properties"), {QStringLiteral("control.exe"), QStringLiteral("sysdm.cpl")}},
        {QStringLiteral("display-settings"), {QStringLiteral("control.exe"), QStringLiteral("desk.cpl")}},
        {QStringLiteral("power-options"), {QStringLiteral("control.exe"), QStringLiteral("powercfg.cpl")}},
        {QStringLiteral("network-connections"), {QStringLiteral("control.exe"), QStringLiteral("ncpa.cpl")}},
        {QStringLiteral("computer-management"), {QStringLiteral("mmc.exe"), QStringLiteral("compmgmt.msc")}},
        {QStringLiteral("control-panel"), {QStringLiteral("control.exe")}},
        {QStringLiteral("disk-management"), {QStringLiteral("mmc.exe"), QStringLiteral("diskmgmt.msc")}},
        {QStringLiteral("task-manager"), {QStringLiteral("taskmgr.exe")}},
        {QStringLiteral("command-line"), {QStringLiteral("cmd.exe")}}};
    if (commands.contains(id))
    {
        auto command = commands.value(id);
        const QString app = command.takeFirst();
        windowsTool(app, command);
    }
}
QDialog *DriverPackShell::detailDialog(int row, int candidate)
{
    const auto e = m_catalog->entry(row);
    if (candidate < 0)
        candidate = e.chosenCandidate;
    DriverCandidate c;
    if (candidate >= 0 && candidate < e.candidates.size())
        c = e.candidates[candidate];
    auto dialog = new RoundedDialog(this);
    dialog->setObjectName(QStringLiteral("driverDetails"));
    dialog->setWindowTitle(t("Driver information", "معلومات التعريف"));
    dialog->setLayoutDirection(UiLanguage::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);
    dialog->resize(qMin(820, width() - 48), qMin(650, height() - 48));
    auto frame = new QVBoxLayout(dialog);
    frame->setContentsMargins(0, 0, 0, 0);
    frame->setSpacing(0);
    frame->addWidget(new DialogHeader(t("Driver information", "معلومات التعريف"),
                                      deviceIcon(e.device.deviceClass), dialog, m_arabic));
    auto content = new QWidget(dialog);
    auto box = new QVBoxLayout(content);
    box->setContentsMargins(22, 18, 22, 22);
    box->setSpacing(16);
    frame->addWidget(content, 1);
    auto hero = new AccentCard(content);
    auto summary = new QVBoxLayout(hero);
    summary->setContentsMargins(20, 16, 20, 18);
    summary->setSpacing(8);
    hero->setStyleSheet(QStringLiteral("QLabel{color:white;} QLabel#caption{color:#c4ebec;}"));
    auto name = copy(e.device.name, hero, "section");
    name->setStyleSheet(QStringLiteral("font-size:19px;font-weight:600;"));
    summary->addWidget(name);
    summary->addWidget(copy((candidate == 0 ? t("Current installed driver", "التعريف المثبت حاليًا")
                                            : t("Catalog driver", "تعريف من الكتالوج")) +
                                QStringLiteral(" · ") + c.version,
                            hero, "caption"));
    box->addWidget(hero);
    auto scroll = new QScrollArea(content);
    scroll->setObjectName(QStringLiteral("driverDetailScroll"));
    scroll->viewport()->setObjectName(QStringLiteral("driverDetailViewport"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumSize(0, 0);
    auto data = new QWidget;
    data->setObjectName(QStringLiteral("driverDetailCanvas"));
    auto cards = new QVBoxLayout(data);
    cards->setContentsMargins(10, 8, 10, 8);
    cards->setSpacing(12);
    auto card = [&](const QString &heading, const QString &symbol, const QString &tone)
    {
        auto face = new SurfaceCard(data);
        face->setProperty("tone", tone);
        auto layout = new QVBoxLayout(face);
        layout->setContentsMargins(18, 15, 18, 20);
        layout->setSpacing(12);
        auto title = new QHBoxLayout;
        auto icon = copy(QString(), face);
        icon->setPixmap(VectorIcons::icon(symbol, QColor("#00aaa9"), 22).pixmap(22, 22));
        icon->setFixedSize(24, 24);
        title->addWidget(icon);
        auto headingLabel = copy(heading, face, "settingTitle");
        headingLabel->setStyleSheet(QStringLiteral("font-size:17px;font-weight:600;"));
        title->addWidget(headingLabel, 1);
        layout->addLayout(title);
        auto fields = new QGridLayout;
        fields->setColumnStretch(0, 2);
        fields->setColumnStretch(1, 5);
        fields->setHorizontalSpacing(18);
        fields->setVerticalSpacing(11);
        layout->addLayout(fields);
        cards->addWidget(face);
        return fields;
    };
    auto field = [&](QGridLayout *fields, const QString &label, const QString &value)
    {
        const int n = fields->rowCount();
        auto heading = copy(label, data, "caption");
        auto text = copy(ltr(value.isEmpty() ? QStringLiteral("—") : value), data);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text->setStyleSheet(QStringLiteral("font-size:14px;font-weight:400;"));
        fields->addWidget(heading, n, 0);
        fields->addWidget(text, n, 1);
    };
    auto release =
        card(t("Version and package", "الإصدار والحزمة"), QStringLiteral("layers"), QStringLiteral("blue"));
    field(release, t("Version", "الإصدار"), c.version);
    field(release, t("Date", "التاريخ"), c.date);
    field(release, t("Provider", "المزود"), c.provider);
    field(release, t("Driver package", "أرشيف التعريف"), QFileInfo(c.packageSource).fileName());
    auto identity =
        card(t("Device and INF", "الجهاز وملف INF"), QStringLiteral("chip"), QStringLiteral("violet"));
    field(identity, QStringLiteral("INF"), c.inf);
    field(identity, t("Section", "القسم"), c.section);
    field(identity, t("Hardware ID", "معرّف الجهاز"),
          c.hardwareId.isEmpty() ? e.device.hardwareId : c.hardwareId);
    field(identity, t("Device instance", "مثيل الجهاز"), e.device.instanceId);
    auto availability = card(t("Availability", "التوفر"), QStringLiteral("shield"), QStringLiteral("teal"));
    field(availability, t("Match / availability", "المطابقة / التوفر"),
          c.matchReason.isEmpty() ? (c.engineToken ? (c.local ? t("Available offline", "متاح أوفلاين")
                                                              : t("Download required", "يتطلب التنزيل"))
                                                   : t("Current installed driver", "التعريف المثبت حاليًا"))
                                  : c.matchReason);
    cards->addStretch();
    scroll->setWidget(data);
    box->addWidget(scroll, 1);
    auto actions = new QHBoxLayout;
    auto device = action(t("Device properties", "خصائص الجهاز"), content);
    device->setIcon(VectorIcons::icon(QStringLiteral("computer"), QColor("#00aaa9"), 20));
    connect(device, &QPushButton::clicked, this,
            [this, e]()
            {
                windowsTool(QStringLiteral("rundll32.exe"),
                            QStringList() << QStringLiteral("devmgr.dll,DeviceProperties_RunDLL")
                                          << QStringLiteral("/DeviceID") << e.device.instanceId);
            });
    actions->addWidget(device, 1);
    auto select = action(t("Select this driver", "تحديد هذا التعريف"), content);
    select->setIcon(VectorIcons::icon(QStringLiteral("check"), QColor("#00aaa9"), 20));
    select->setEnabled((c.engineToken && c.valid) || (candidate == 0 && reinstallCandidateIndex(e) > 0));
    connect(select, &QPushButton::clicked, this,
            [this, row, candidate, dialog]()
            {
                m_remoteChoices[m_catalog->entry(row).device.instanceId] = candidate;
                setDriver(row, true);
                dialog->accept();
                expert(0);
            });
    actions->addWidget(select, 1);
    box->addLayout(actions);
    return dialog;
}
void DriverPackShell::details(int row, int candidate)
{
    auto dialog = detailDialog(row, candidate);
    dialog->exec();
    delete dialog;
}
bool DriverPackShell::confirm(const QString &title, const QString &message)
{
    if (m_capture)
        return false;
    return projectMessage(this, title, message, true, m_arabic);
}
void DriverPackShell::showMessage(const QString &title, const QString &message)
{
    if (m_capture)
    {
        m_error = message;
        return;
    }
    projectMessage(this, title, message, false, m_arabic);
}
void DriverPackShell::refreshChrome()
{
    if (auto bar = qobject_cast<TitleBar *>(m_header))
    {
        bar->setDark(m_dark);
        bar->setArabic(m_arabic);
        bar->setMaximized(isMaximized());
        if (auto button = bar->findChild<QToolButton *>(QStringLiteral("titleSidebarToggle")))
            button->setVisible(m_root && m_root->currentWidget() == m_expert);
    }
}
void DriverPackShell::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_tools && m_header)
        m_tools->setGeometry(0, m_header->height(), width(), height() - m_header->height());
}
void DriverPackShell::languageDialog()
{
    if (busy())
        return;
    RoundedDialog dialog(this);
    dialog.setWindowTitle(t("Language", "اللغة"));
    auto box = new QVBoxLayout(&dialog);
    box->setContentsMargins(24, 22, 24, 22);
    box->addWidget(copy(t("Select application language", "اختر لغة البرنامج"), &dialog, "section"));
    for (int i = 0; i < 2; ++i)
    {
        auto choice = action(i ? QString::fromUtf8("العربية") : QStringLiteral("English"), &dialog);
        choice->setCheckable(true);
        choice->setChecked(m_arabic == (i == 1));
        box->addWidget(choice);
        connect(choice, &QPushButton::clicked, &dialog, [&dialog, i]() { dialog.done(100 + i); });
    }
    const int result = dialog.exec();
    if (result >= 100)
        setLanguage(result == 101);
}
void DriverPackShell::reportDialog()
{
    RoundedDialog dialog(this);
    dialog.setWindowTitle(t("Report an error", "الإبلاغ عن مشكلة"));
    dialog.setLayoutDirection(UiLanguage::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);
    dialog.resize(qMin(700, width() - 60), qMin(600, height() - 60));
    auto frame = new QVBoxLayout(&dialog);
    frame->setContentsMargins(0, 0, 0, 0);
    frame->setSpacing(0);
    frame->addWidget(
        new DialogHeader(t("Report an error", "الإبلاغ عن مشكلة"), QStringLiteral("bug"), &dialog, m_arabic));
    auto content = new QWidget(&dialog);
    auto box = new QVBoxLayout(content);
    box->setContentsMargins(24, 18, 24, 22);
    box->setSpacing(12);
    frame->addWidget(content, 1);
    box->addWidget(copy(t("Describe what happened and how to reproduce it. Reports stay local until you "
                          "choose to share them.",
                          "صف ما حدث وخطوات تكرار المشكلة. يبقى التقرير محليًا حتى تختار مشاركته."),
                        &dialog, "caption"));
    auto description = new QPlainTextEdit(&dialog);
    description->setObjectName(QStringLiteral("bugDescription"));
    description->setPlaceholderText(
        t("Steps · Expected result · Actual result", "الخطوات · النتيجة المتوقعة · ما حدث فعليًا"));
    box->addWidget(description, 1);
    auto environment = new QPlainTextEdit(&dialog);
    environment->setObjectName(QStringLiteral("bugEnvironment"));
    environment->setReadOnly(true);
    environment->setMaximumHeight(130);
    environment->setPlainText(BugReport::environment(QSysInfo::prettyProductName(),
                                                     QSysInfo::currentCpuArchitecture(), m_language, m_dark) +
                              QStringLiteral("\n") + m_error);
    box->addWidget(environment);
    auto include = new QCheckBox(t("Include detailed component information (hardware IDs and serial numbers)",
                                   "تضمين تفاصيل المكونات (معرّفات الأجهزة والأرقام التسلسلية)"),
                                 &dialog);
    box->addWidget(include);
    auto commands = new QHBoxLayout;
    auto clip = action(t("Copy report", "نسخ التقرير"), &dialog);
    connect(clip, &QPushButton::clicked, &dialog,
            [description, environment]()
            {
                QApplication::clipboard()->setText(description->toPlainText() + QStringLiteral("\n\n") +
                                                   environment->toPlainText());
            });
    commands->addWidget(clip);
    auto save = action(t("Save report…", "حفظ التقرير…"), &dialog);
    connect(save, &QPushButton::clicked, &dialog,
            [this, description, environment, include]()
            {
                const QString file = QFileDialog::getSaveFileName(
                    this, t("Save bug report", "حفظ بلاغ المشكلة"),
                    QStringLiteral("DriverPack-bug-report.json"), QStringLiteral("JSON (*.json)"));
                if (file.isEmpty())
                    return;
                QJsonObject report;
                report.insert(QStringLiteral("format"), QStringLiteral("DriverPackModernBugReport"));
                report.insert(QStringLiteral("description"), description->toPlainText());
                report.insert(QStringLiteral("environment"), environment->toPlainText());
                report.insert(QStringLiteral("automaticUpload"), false);
                if (include->isChecked())
                    report.insert(QStringLiteral("hardware"), m_hardwareData);
                QSaveFile output(file);
                const QByteArray bytes = QJsonDocument(report).toJson();
                if (output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size())
                    output.commit();
            });
    commands->addWidget(save);
    auto github=action(t("Open GitHub issue…","فتح بلاغ على GitHub…"),&dialog);
    connect(github,&QPushButton::clicked,&dialog,[this,description,environment](){if(m_capture||m_demo)return;QUrl url=BugReport::issueUrl(environment->toPlainText());QUrlQuery query(url);query.addQueryItem(QStringLiteral("steps"),description->toPlainText().left(4000));url.setQuery(query);QDesktopServices::openUrl(url);});commands->addWidget(github);
    commands->addStretch();
    auto close = action(t("Close", "إغلاق"), &dialog);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    commands->addWidget(close);
    box->addLayout(commands);
    dialog.exec();
}

class DrpSoftwareFetch : public QThread
{
  public:
    QString source, folder, path, error, targetName;
    std::atomic<bool> stop{false};
    std::atomic<qint64> received{0}, total{0};
    explicit DrpSoftwareFetch(QObject *p) : QThread(p) {}

  protected:
    void run() Q_DECL_OVERRIDE
    {
        try
        {
#ifdef Q_OS_WIN
            QUrl url(source);
            if (!DriverPackSoftware::allowedUrl(source))
                throw std::runtime_error("Unsupported software URL.");
            url.setScheme(QStringLiteral("https"));
            const QString name = targetName.isEmpty() ? QFileInfo(url.path()).fileName() : targetName;
            if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
                name.contains(QLatin1Char(':')) || name.contains(QLatin1Char('%')) ||
                name.contains(QStringLiteral("..")) ||
                !name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) || name.size() > 180)
                throw std::runtime_error("Invalid installer filename.");
            if (!QDir().mkpath(folder))
                throw std::runtime_error("Cannot create the local software directory.");
            QFileInfo directory(folder);
            if (directory.isSymLink() || directory.canonicalFilePath().isEmpty())
                throw std::runtime_error("The software directory must be a normal local folder.");
            path = QDir(directory.canonicalFilePath()).filePath(name);
            if (QFileInfo(path).exists())
                throw std::runtime_error(
                    "A setup with this name already exists. Refresh and review it instead of replacing it.");
            struct Handle
            {
                HINTERNET value = 0;
                ~Handle()
                {
                    if (value)
                        InternetCloseHandle(value);
                }
            } internet, request;
            internet.value = InternetOpenW(L"DriverPackModern/0.4", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
            if (!internet.value)
                throw std::runtime_error("Cannot initialize the Windows HTTPS client.");
            DWORD timeout = 12000;
            InternetSetOptionW(internet.value, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
            InternetSetOptionW(internet.value, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
            const QString address = url.toString(QUrl::FullyEncoded);
            request.value = InternetOpenUrlW(internet.value, reinterpret_cast<LPCWSTR>(address.utf16()), 0, 0,
                                             INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                                                 INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI,
                                             0);
            if (!request.value)
                throw std::runtime_error("The software download host could not be reached over HTTPS.");
            DWORD code = 0, length = sizeof(code);
            if (!HttpQueryInfoW(request.value, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code,
                                &length, 0) ||
                code != 200)
                throw std::runtime_error("The software server did not return a downloadable installer.");
            DWORD size = 0;
            length = sizeof(size);
            if (HttpQueryInfoW(request.value, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &size,
                               &length, 0))
                total.store(size);
            if (size > 1024UL * 1024 * 1024)
                throw std::runtime_error("Installer exceeds the 1 GB software download limit.");
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly))
                throw std::runtime_error("Cannot stage the software download.");
            QByteArray buffer(65536, 0), signature;
            for (;;)
            {
                if (stop.load())
                    throw std::runtime_error("Software download cancelled.");
                DWORD count = 0;
                if (!InternetReadFile(request.value, buffer.data(), buffer.size(), &count))
                    throw std::runtime_error("Software transfer failed; no installer was published.");
                if (!count)
                    break;
                if (received.load() + count > 1024LL * 1024 * 1024)
                    throw std::runtime_error("Software download exceeded its size limit.");
                if (signature.size() < 2)
                    signature.append(buffer.left(qMin<int>(count, 2 - signature.size())));
                if (output.write(buffer.constData(), count) != count)
                    throw std::runtime_error("Cannot write the software download.");
                received.fetch_add(count);
            }
            if (signature != QByteArray("MZ") || received.load() < 2 ||
                (total.load() > 0 && received.load() != total.load()))
                throw std::runtime_error("The downloaded file is not a complete Windows executable.");
            if (stop.load() || QFileInfo(path).exists())
                throw std::runtime_error(
                    "Download cancelled or a file appeared at the destination; no installer was replaced.");
            if (!output.commit())
                throw std::runtime_error("Cannot publish the downloaded installer.");
#else
            throw std::runtime_error("Windows HTTPS client required.");
#endif
        }
        catch (const std::exception &e)
        {
            error = QString::fromUtf8(e.what());
        }
    }
};
DriverPackShell::~DriverPackShell()
{
    m_hardware->stop.store(true);
    m_hardware->wait();
    if (m_fetch)
    {
        m_fetch->stop.store(true);
        m_fetch->wait();
    }
#ifdef Q_OS_WIN
    if (m_setup)
        CloseHandle(static_cast<HANDLE>(m_setup));
#endif
}
void DriverPackShell::installationPage()
{
    if (m_install->layout())
    {
        qDeleteAll(m_install->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
        delete m_install->layout();
    }
    auto box = new QVBoxLayout(m_install);
    box->setContentsMargins(48, 35, 48, 30);
    box->setSpacing(22);
    box->addWidget(copy(t("Configuring your computer", "جارٍ إعداد الكمبيوتر"), m_install, "title"));
    m_installText =
        copy(t("Preparing the selected items…", "جارٍ تجهيز العناصر المحددة…"), m_install, "largeCaption");
    box->addWidget(m_installText);
    m_installBar = new QProgressBar(m_install);
    m_installBar->setRange(0, 0);
    box->addWidget(m_installBar);
    m_elapsedText = copy(QString(), m_install, "caption");
    box->addWidget(m_elapsedText);
    m_installItems = new ProjectTable(m_install);
    m_installItems->setRootIsDecorated(false);
    m_installItems->setHeaderLabels(QStringList() << t("Driver / software", "التعريف / البرنامج")
                                                  << t("Status", "الحالة") << t("Time", "الوقت"));
    m_installItems->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_installItems->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_installItems->setColumnWidth(2, 100);
    m_installItems->setHeaderHidden(true);
    m_installItems->setIndentation(0);
    m_installItemMap.clear();
    m_itemStarted.clear();
    m_activeItem.clear();
    QTreeWidgetItem *drivers = 0, *programs = 0;
    if (!m_downloadOnly &&
        m_settings->value(QStringLiteral("installation/restorePointRequired"), true).toBool())
    {
        auto restore = new QTreeWidgetItem(m_installItems);
        restore->setText(0, t("System restore point", "نقطة استعادة النظام"));
        restore->setText(1, t("Waiting", "بانتظار الدور"));
        m_installItemMap.insert(QStringLiteral("restore"), restore);
    }
    for (auto it = m_resultRows.begin(); it != m_resultRows.end(); ++it)
    {
        const bool software = it.key().startsWith(QStringLiteral("software:"));
        QTreeWidgetItem *&group = software ? programs : drivers;
        if (!group)
        {
            group = new QTreeWidgetItem(m_installItems);
            group->setText(0, software ? t("Software", "البرامج") : t("Drivers", "التعريفات"));
            QFont face = font();
            face.setPixelSize(23);
            group->setFont(0, face);
            group->setSizeHint(0, QSize(0, 55));
            group->setExpanded(true);
        }
        auto item = new QTreeWidgetItem(group);
        item->setText(0, m_resultNames.value(it.value()));
        item->setText(1, t("Waiting", "بانتظار الدور"));
        item->setSizeHint(0, QSize(0, 60));
        m_installItemMap.insert(it.key(), item);
    }
    box->addWidget(m_installItems, 1);
    auto cancel = action(t("Stop the remaining queue", "إيقاف باقي القائمة"), m_install);
    connect(cancel, &QPushButton::clicked, this,
            [this]()
            {
                if (m_fetch && m_fetch->isRunning())
                    m_fetch->stop.store(true);
                m_pendingSoftware = false;
                m_cancelSoftware = true;
                if (m_softwareActive && m_setup)
                    m_softwareQueue.resize(qMin(m_softwareQueue.size(), m_softwarePosition + 1));
                else if (!m_fetch)
                    m_softwareQueue.clear();
                if (m_engine->isBusy())
                    m_engine->requestStopAfterCurrent();
                else if (m_softwareActive)
                {
                    if (m_setup)
                        showMessage(t("Setup is still open", "المثبت ما زال مفتوحًا"),
                                    t("Close or cancel its own wizard. No remaining setup will be started.",
                                      "أكمل معالج المثبت المفتوح أو ألغِه. لن يبدأ أي مثبت آخر."));
                    else
                    {
                        m_softwareActive = false;
                        m_setupTimer->stop();
                        m_clock->stop();
                        finalPage();
                    }
                }
            });
    box->addWidget(cancel, 0, Qt::AlignTrailing);
    m_root->setCurrentWidget(m_install);
    m_elapsed.restart();
    m_clock->start();
}
void DriverPackShell::startInstall()
{
    if (busy() || m_capture || m_demo)
        return;
    const bool drivers = !m_softwareOnly && m_singleSoftware.isEmpty() && m_catalog->selectedDrivers() > 0;
    const QSet<QString> software =
        m_singleSoftware.isEmpty() ? m_programSelection : (QSet<QString>() << m_singleSoftware);
    if (!m_softwareOnly && m_singleSoftware.isEmpty() && !m_remote.isEmpty())
    {
        showMessage(
            t("Download required packages", "تنزيل الحزم المطلوبة"),
            t("Some selected drivers are not downloaded. Download their packages first, then review the "
              "refreshed matches before installation.",
              "بعض التعريفات المحددة غير محمّلة. نزّل حزمها أولًا ثم راجع المطابقة المحدثة قبل التثبيت."));
        downloads();
        return;
    }
    if (!drivers && software.isEmpty())
    {
        beginScan();
        return;
    }
    QStringList names = drivers ? m_catalog->selectedNames() : QStringList();
    for (const auto &p : m_programs)
        if (software.contains(p.id))
            names << p.name;
    const bool restore =
        m_settings->value(QStringLiteral("installation/restorePointRequired"), true).toBool();
    QString review =
        t("Review the items below before changing this computer.\n\n%1\n\nRestore point: %2\nSoftware setups "
          "are interactive; original advertising and browser-default switches are not supplied.",
          "راجع العناصر التالية قبل تغيير هذا الكمبيوتر.\n\n%1\n\nنقطة الاستعادة: %2\nمثبتات البرامج تفاعلية "
          "ولا تُمرر مفاتيح الإعلانات أو تغيير المتصفح الأصلية.")
            .arg(names.join(QLatin1Char('\n')), restore ? t("On", "مفعلة") : t("Off", "معطلة"));
    if (drivers)
        for (const auto &entry : m_catalog->entries())
            if (entry.checked && entry.chosenCandidate > 0)
            {
                const auto &candidate = entry.candidates[entry.chosenCandidate];
                if (candidate.statusBits & 0x80u)
                    review += QLatin1Char('\n') +
                              t("Downgrade: %1 · %2 → %3", "إصدار أقدم: %1 · %2 ← %3")
                                  .arg(entry.device.name, entry.device.driverVersion, candidate.version);
                else if (isReinstallCandidate(entry, entry.chosenCandidate))
                    review += QLatin1Char('\n') + t("Reinstall: %1 · %2", "إعادة تثبيت: %1 · %2")
                                                      .arg(entry.device.name, candidate.version);
            }
    if (!confirm(t("Confirm computer configuring", "تأكيد إعداد الكمبيوتر"), review))
        return;
    if (drivers && !UiScale::administrator())
    {
        QStringList ids;
        for (const auto &e : m_catalog->entries())
            if (e.checked)
                ids << e.device.instanceId;
        m_settings->setValue(QStringLiteral("driverpack/pendingReview"), ids);
        m_settings->setValue(QStringLiteral("driverpack/pendingSoftwareReview"),
                             QStringList(m_programSelection.toList()));
        m_settings->sync();
        if (confirm(t("Administrator access", "صلاحيات المسؤول"),
                    t("Reopen as Administrator? The selected items will be reviewed again; installation will "
                      "not start automatically.",
                      "إعادة الفتح كمسؤول؟ ستتم مراجعة العناصر مجددًا ولن يبدأ التثبيت تلقائيًا.")))
        {
            const QString error = UiScale::restartElevated();
            if (error.isEmpty())
                QCoreApplication::quit();
            else
                showMessage(t("Administrator restart", "إعادة الفتح كمسؤول"), error);
        }
        return;
    }
    if (m_settings->value(QStringLiteral("driverpack/systemCheck"), true).toBool())
    {
        QStorageInfo temp(QDir::tempPath());
        if (temp.isValid() && temp.bytesAvailable() < 64 * 1024 * 1024)
        {
            showMessage(
                t("System check", "فحص النظام"),
                t("The Windows temporary drive has less than 64 MB free. Free space before starting.",
                  "المساحة المتاحة بقرص الملفات المؤقتة لويندوز أقل من 64 ميجابايت. وفّر مساحة قبل البدء."));
            return;
        }
    }
    m_downloadOnly = false;
    m_resultDownloadOnly = false;
    m_restoreFailed = false;
    m_error.clear();
    m_resultNames = names;
    m_resultStates.clear();
    m_resultRows.clear();
    m_resultFailed = false;
    m_lastReboot = false;
    int row = 0;
    for (const auto &e : m_catalog->entries())
        if (drivers && e.checked)
            m_resultRows.insert(e.device.instanceId, row++);
    for (const auto &p : m_programs)
        if (software.contains(p.id))
            m_resultRows.insert(QStringLiteral("software:") + p.id, row++);
    m_activeSoftware = software;
    m_pendingSoftware = !software.isEmpty();
    installationPage();
    if (drivers)
    {
        auto plan = m_catalog->buildPlan(restore, true, true);
        try
        {
            sdiomodern::validatePlan(m_catalog->nativeSnapshot(), plan);
        }
        catch (const std::exception &e)
        {
            m_error = QString::fromUtf8(e.what());
            m_resultFailed = true;
            finalPage();
            return;
        }
        applyOptions();
        m_installing = true;
        if (!m_engine->requestInstallation(plan))
        {
            m_installing = false;
            m_resultFailed = true;
            finalPage();
        }
    }
    else
        beginSoftware();
}
void DriverPackShell::nativeProgress(const sdiomodern::Event &e)
{
    if (!m_installing)
    {
        m_loadText->setText(e.stage == sdiomodern::Stage::Scan
                                ? t("Reading Windows devices…", "جارٍ قراءة أجهزة ويندوز…")
                            : e.stage == sdiomodern::Stage::Indexes
                                ? t("Reading the DriverPack index…", "جارٍ قراءة فهرس درايفر باك…")
                                : t("Matching driver packages…", "جارٍ مطابقة حزم التعريفات…"));
        return;
    }
    m_installText->setText((e.stage == sdiomodern::Stage::RestorePoint
                                ? t("Creating a restore point…", "جارٍ إنشاء نقطة استعادة…")
                            : e.stage == sdiomodern::Stage::Extract
                                ? t("Extracting driver files…", "جارٍ استخراج ملفات التعريف…")
                            : e.stage == sdiomodern::Stage::Install
                                ? t("Installing the selected driver…", "جارٍ تثبيت التعريف المحدد…")
                                : t("Preparing the selected items…", "جارٍ تجهيز العناصر المحددة…")) +
                           QStringLiteral("  ") + QString::fromStdWString(e.label));
    if (e.percent >= 0)
    {
        m_installBar->setRange(0, 100);
        m_installBar->setValue(e.percent);
    }
    else if (e.total)
    {
        m_installBar->setRange(0, 1000);
        m_installBar->setValue(e.done * 1000 / e.total);
    }
    const QString id = QString::fromStdWString(e.deviceId);
    if (e.stage == sdiomodern::Stage::RestorePoint && m_installItemMap.contains(QStringLiteral("restore")))
        m_installItemMap.value(QStringLiteral("restore"))->setText(1, t("Creating…", "جارٍ الإنشاء…"));
    if (!id.isEmpty() && m_installItemMap.contains(id))
    {
        if (!m_itemStarted.contains(id))
            m_itemStarted[id] = m_elapsed.elapsed();
        m_activeItem = id;
    }
    if (m_resultRows.contains(id))
    {
        auto row = m_installItemMap.value(id);
        row->setText(1, e.outcome == sdiomodern::Outcome::Success   ? t("Installed", "تم التثبيت")
                        : e.outcome == sdiomodern::Outcome::Failure ? t("Failed", "فشل")
                                                                    : t("Working…", "جارٍ العمل…"));
        row->setText(2, QStringLiteral("%1 s").arg(
                            (m_elapsed.elapsed() - m_itemStarted.value(id, m_elapsed.elapsed())) / 1000));
    }
}
void DriverPackShell::nativeFinished(const sdiomodern::InstallReport &r)
{
    m_installing = false;
    m_activeItem.clear();
    if (m_installItemMap.contains(QStringLiteral("restore")))
        m_installItemMap.value(QStringLiteral("restore"))
            ->setText(1, r.restoreCreated  ? t("Created", "تم الإنشاء")
                         : r.restoreFailed ? t("Failed", "فشل")
                                           : t("Not created", "لم تُنشأ"));
    m_lastReboot = r.rebootRequired;
    m_restoreFailed = r.restoreFailed;
    m_error = QString::fromStdWString(r.error);
    m_resultFailed = !DriverPackWorkflow::allowSoftwareAfterDrivers(r);
    for (const auto &item : r.items)
    {
        const QString id = QString::fromStdWString(item.deviceId);
        const QString state = item.outcome == sdiomodern::Outcome::Success   ? t("Installed", "تم التثبيت")
                              : item.outcome == sdiomodern::Outcome::Failure ? t("Failed", "فشل")
                                                                             : t("Not installed", "لم يُثبَّت");
        m_resultStates.insert(id, state);
        if (m_resultRows.contains(id))
            m_installItemMap.value(id)->setText(1, state);
    }
    if (!m_resultFailed && m_pendingSoftware)
    {
        QTimer::singleShot(0, this, [this]() { beginSoftware(); });
        return;
    }
    m_pendingSoftware = false;
    m_clock->stop();
    m_catalog->invalidateNative();
    finalPage();
}
void DriverPackShell::downloadSoftwareOnly()
{
    if (m_capture || m_demo || busy())
        return;
    loadSoftware();
    m_activeSoftware.clear();
    m_resultRows.clear();
    m_resultNames.clear();
    m_resultStates.clear();
    m_resultFailed = false;
    m_error.clear();
    m_restoreFailed = false;
    m_lastReboot = false;
    m_downloadOnly = true;
    m_resultDownloadOnly = true;
    int row = 0;
    for (const auto &program : m_programs)
        if (!program.excluded && DriverPackSoftware::allowedUrl(program.url))
        {
            m_activeSoftware.insert(program.id);
            m_resultRows.insert(QStringLiteral("software:") + program.id, row++);
            m_resultNames << program.name;
        }
    if (m_activeSoftware.isEmpty())
    {
        showMessage(t("Offline software", "البرامج الأوفلاين"),
                    t("No supported software packages are available.", "لا توجد حزم برامج مدعومة متاحة."));
        return;
    }
    installationPage();
    m_installText->setText(t("Downloading software for offline use. No setup will be started.",
                             "جارٍ تنزيل البرامج للاستخدام الأوفلاين. لن يبدأ أي مثبت."));
    beginSoftware();
}
void DriverPackShell::beginSoftware()
{
    m_pendingSoftware = false;
    m_cancelSoftware = false;
    m_softwareQueue.clear();
    for (int i = 0; i < m_programs.size(); ++i)
        if (m_activeSoftware.contains(m_programs[i].id))
            m_softwareQueue << i;
    m_softwarePosition = 0;
    m_softwareActive = true;
    nextSoftware();
}
void DriverPackShell::nextSoftware()
{
    if (m_cancelSoftware && !m_setup && !(m_fetch && m_fetch->isRunning()))
        m_softwareQueue.clear();
    if (m_softwarePosition >= m_softwareQueue.size())
    {
        m_softwareActive = false;
        m_setupTimer->stop();
        m_clock->stop();
        loadSoftware();
        if (!m_downloadOnly)
            for (const QString &id : m_activeSoftware)
                m_programSelection.remove(id);
        m_activeSoftware.clear();
        if (!m_downloadOnly)
            m_catalog->invalidateNative();
        finalPage();
        return;
    }
    auto &program = m_programs[m_currentProgram = m_softwareQueue[m_softwarePosition]];
    const QString id = QStringLiteral("software:") + program.id;
    m_activeItem = id;
    if (!m_itemStarted.contains(id))
        m_itemStarted[id] = m_elapsed.elapsed();
    if (m_resultRows.contains(id))
    {
        auto row = m_installItemMap.value(id);
        row->setText(1, t("Preparing setup", "تجهيز المثبت"));
    }
    const QString file = DriverPackSoftware::localProgram(m_library, program, !m_downloadOnly);
    if (file.isEmpty())
    {
        if (!DriverPackSoftware::allowedUrl(program.url))
        {
            m_resultFailed = true;
            m_error = t("This program has no supported static download URL.",
                        "لا يملك هذا البرنامج رابط تنزيل ثابتًا مدعومًا.");
            m_softwareQueue.clear();
            nextSoftware();
            return;
        }
        m_fetch = new DrpSoftwareFetch(this);
        m_fetch->source = program.url;
        m_fetch->targetName = program.fileName;
        m_fetch->folder = QDir(m_library).filePath(QStringLiteral("programs/") +
                                                   DriverPackSoftware::categoryFolder(program.category));
        m_installText->setText(t("Downloading %1…", "جارٍ تنزيل %1…").arg(program.name));
        connect(m_fetch, &QThread::finished, this,
                [this]()
                {
                    auto fetch = m_fetch;
                    m_fetch = 0;
                    if (m_cancelSoftware)
                    {
                        m_softwareQueue.clear();
                        fetch->deleteLater();
                        nextSoftware();
                        return;
                    }
                    if (!fetch->error.isEmpty())
                    {
                        m_resultFailed = true;
                        m_error = fetch->error;
                        m_softwareQueue.clear();
                        fetch->deleteLater();
                        nextSoftware();
                        return;
                    }
                    if (m_currentProgram >= 0 && m_currentProgram < m_programs.size())
                        m_programs[m_currentProgram].localPath = fetch->path;
                    fetch->deleteLater();
                    nextSoftware();
                });
        m_fetch->start();
        m_setupTimer->start();
        return;
    }
    if (m_downloadOnly)
    {
        const QString state = t("Available offline", "متاح أوفلاين");
        m_resultStates[id] = state;
        if (m_installItemMap.contains(id))
            m_installItemMap[id]->setText(1, state);
        ++m_softwarePosition;
        QTimer::singleShot(0, this, [this]() { nextSoftware(); });
        return;
    }
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(file), folder = QFileInfo(file).absolutePath();
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.hwnd = reinterpret_cast<HWND>(winId());
    info.lpVerb = L"open";
    info.lpFile = reinterpret_cast<LPCWSTR>(native.utf16());
    info.lpDirectory = reinterpret_cast<LPCWSTR>(folder.utf16());
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info) || !info.hProcess)
    {
        m_resultFailed = true;
        m_error = t("The setup was cancelled or could not start. Windows error: %1",
                    "أُلغي المثبت أو تعذّر تشغيله. خطأ ويندوز: %1")
                      .arg(GetLastError());
        m_softwareQueue.clear();
        nextSoftware();
        return;
    }
    m_setup = info.hProcess;
    m_installText->setText(
        t("%1 setup is open. Follow its wizard to continue.", "مثبت %1 مفتوح. اتبع معالجه للمتابعة.")
            .arg(program.name));
    m_installBar->setRange(0, 100);
    m_installBar->setValue(m_softwarePosition * 100 / qMax(1, m_softwareQueue.size()));
    m_setupTimer->start();
#else
    m_error = t("Windows installer required.", "يتطلب مثبت ويندوز.");
    m_resultFailed = true;
    m_softwareQueue.clear();
    nextSoftware();
#endif
}
void DriverPackShell::pollSoftware()
{
    if (m_fetch && m_fetch->isRunning())
    {
        const qint64 total = m_fetch->total.load();
        if (total > 0)
        {
            m_installBar->setRange(0, 1000);
            m_installBar->setValue(m_fetch->received.load() * 1000 / total);
        }
        return;
    }
#ifdef Q_OS_WIN
    if (!m_setup)
        return;
    DWORD code = STILL_ACTIVE;
    if (GetExitCodeProcess(static_cast<HANDLE>(m_setup), &code) && code != STILL_ACTIVE)
    {
        CloseHandle(static_cast<HANDLE>(m_setup));
        m_setup = 0;
        const auto program = m_programs[m_currentProgram];
        const QString id = QStringLiteral("software:") + program.id;
        const bool success = code == 0 || code == 3010;
        QString state = success ? t("Setup finished", "انتهى المثبت")
                                : t("Setup failed / cancelled", "فشل المثبت أو أُلغي");
        m_resultStates.insert(id, state);
        if (m_resultRows.contains(id))
            m_installItemMap.value(id)->setText(1, state);
        if (!success)
        {
            m_resultFailed = true;
            m_error = t("Software setup returned Windows code %1. Remaining setups were stopped.",
                        "أعاد المثبت رمز ويندوز %1. تم إيقاف باقي المثبتات.")
                          .arg(code);
            m_softwareQueue.clear();
        }
        else
        {
            if (code == 3010)
                m_lastReboot = true;
            ++m_softwarePosition;
        }
        nextSoftware();
    }
#endif
}
void DriverPackShell::finalPage()
{
    m_clock->stop();
    if (m_final->layout())
    {
        qDeleteAll(m_final->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
        delete m_final->layout();
    }
    auto box = new QVBoxLayout(m_final);
    box->setContentsMargins(80, 50, 80, 45);
    box->setSpacing(24);
    auto symbol = copy(m_resultFailed ? QStringLiteral("!") : QStringLiteral("✓"), m_final, "startTitle");
    symbol->setAlignment(Qt::AlignCenter);
    symbol->setStyleSheet(QStringLiteral("font-size:82px;color:%1;")
                              .arg(m_resultFailed ? QStringLiteral("#e59b39") : QStringLiteral("#00b5a6")));
    box->addWidget(symbol);
    auto title = copy(
        m_capture
            ? t("Result screen preview — no installation performed", "معاينة شاشة النتائج — لم يُنفّذ تثبيت")
        : m_resultDownloadOnly ? (m_resultFailed ? t("Offline download completed with errors",
                                                     "اكتمل التنزيل الأوفلاين مع أخطاء")
                                                 : t("Offline software is ready", "البرامج الأوفلاين جاهزة"))
        : m_resultFailed
            ? t("Computer configuring completed with errors", "اكتمل إعداد الكمبيوتر مع وجود أخطاء")
            : t("Computer configuring completed", "اكتمل إعداد الكمبيوتر"),
        m_final, "startTitle");
    title->setAlignment(Qt::AlignCenter);
    box->addWidget(title);
    if (!m_error.isEmpty())
        box->addWidget(copy(m_error, m_final, "caption"));
    auto results = new ProjectTable(m_final);
    results->setRootIsDecorated(false);
    results->setHeaderLabels(QStringList() << t("Item", "العنصر") << t("Result", "النتيجة"));
    results->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (auto it = m_resultRows.begin(); it != m_resultRows.end(); ++it)
    {
        auto row = new QTreeWidgetItem(results);
        row->setText(0, m_resultNames.value(it.value()));
        row->setText(1, m_resultStates.value(it.key(), t("Not started", "لم يبدأ")));
    }
    box->addWidget(results, 1);
    box->addWidget(
        copy(m_resultDownloadOnly
                 ? t("Packages were saved for offline use. No installer was started.",
                     "حُفظت الحزم للاستخدام الأوفلاين. لم يتم تشغيل أي مثبت.")
                 : t("Software setup completion is not an installed-program guarantee. Registered state "
                     "is re-read from Windows.",
                     "انتهاء مثبت البرنامج لا يضمن تسجيل تثبيته. تُقرأ الحالة المسجلة مجددًا من ويندوز."),
             m_final, "caption"));
    auto commands = new QHBoxLayout;
    auto back = action(m_resultDownloadOnly ? t("Return to offline packages", "العودة لحزم الأوفلاين")
                                            : t("Refresh and return to drivers", "تحديث والعودة للتعريفات"),
                       m_final, true);
    connect(back, &QPushButton::clicked, this,
            [this]()
            {
                if (m_resultDownloadOnly)
                {
                    expert(4);
                    return;
                }
                m_page = 0;
                beginScan();
            });
    commands->addWidget(back);
    if (m_restoreFailed)
    {
        auto protection = action(t("System Protection", "حماية النظام"), m_final);
        connect(protection, &QPushButton::clicked, this,
                [this]() { windowsTool(QStringLiteral("SystemPropertiesProtection.exe")); });
        commands->addWidget(protection);
    }
    if (m_lastReboot && m_settings->value(QStringLiteral("installation/rebootAfter"), false).toBool())
    {
        auto restart = action(t("Restart computer…", "إعادة تشغيل الكمبيوتر…"), m_final);
        restart->setObjectName(QStringLiteral("resultRestartAction"));
        connect(restart, &QPushButton::clicked, this,
                [this]()
                {
                    if (confirm(t("Restart Windows", "إعادة تشغيل ويندوز"),
                                t("Save your work, then restart this computer now?",
                                  "احفظ عملك ثم أعد تشغيل الكمبيوتر الآن؟")))
                        windowsTool(QStringLiteral("shutdown.exe"), QStringList() << QStringLiteral("/r")
                                                                                  << QStringLiteral("/t")
                                                                                  << QStringLiteral("0"));
                });
        commands->addWidget(restart);
    }
    box->addLayout(commands);
    m_root->setCurrentWidget(m_final);
}
int DriverPackShell::capture(const QString &folder)
{
    if (!m_capture || qgetenv("DRIVERPACK_MODERN_TEST_SETTINGS").isEmpty())
        return 50;
    setAttribute(Qt::WA_DontShowOnScreen);
    show();
    const bool restartPreference =
        m_settings->value(QStringLiteral("installation/rebootAfter"), false).toBool();
    m_lastReboot = true;
    m_settings->setValue(QStringLiteral("installation/rebootAfter"), false);
    finalPage();
    if (m_final->findChild<QPushButton *>(QStringLiteral("resultRestartAction")))
        return 154;
    m_settings->setValue(QStringLiteral("installation/rebootAfter"), true);
    finalPage();
    if (!m_final->findChild<QPushButton *>(QStringLiteral("resultRestartAction")))
        return 155;
    m_lastReboot = false;
    finalPage();
    if (m_final->findChild<QPushButton *>(QStringLiteral("resultRestartAction")))
        return 156;
    m_settings->setValue(QStringLiteral("installation/rebootAfter"), restartPreference);
    int dragChecks = 0, cellChecks = 0, expansionChecks = 0;
    int nativeFrameChecks = 0;
#ifdef Q_OS_WIN
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        showMaximized();
        events(40);
        showNormal();
        events(40);
        showMinimized();
        events(40);
        showNormal();
        events(40);
        HWND handle = reinterpret_cast<HWND>(winId());
        if (SendMessageW(handle, WM_NCACTIVATE, FALSE, 0) != TRUE ||
            SendMessageW(handle, WM_NCACTIVATE, TRUE, 0) != TRUE)
            return 126;
        SendMessageW(handle, WM_NCPAINT, 1, 0);
        RECT windowRect = {}, client = {};
        POINT origin = {};
        GetWindowRect(handle, &windowRect);
        GetClientRect(handle, &client);
        ClientToScreen(handle, &origin);
        if ((GetWindowLongW(handle, GWL_STYLE) & WS_CAPTION) != 0 || origin.x != windowRect.left ||
            origin.y != windowRect.top || client.right != windowRect.right - windowRect.left ||
            client.bottom != windowRect.bottom - windowRect.top)
            return 127;
        ++nativeFrameChecks;
    }
#endif
    int toggles = 0;
    bool opened = false;
    auto interaction = new ClickRow(this);
    interaction->resize(80, 40);
    interaction->clicked = [&toggles]() { toggles = 1 - toggles; };
    interaction->doubled = [&opened]() { opened = true; };
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(10, 10), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(interaction, &release);
    if (toggles != 1)
        return 62;
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(10, 10), Qt::LeftButton, Qt::LeftButton,
                            Qt::NoModifier);
    QApplication::sendEvent(interaction, &doubleClick);
    QApplication::sendEvent(interaction, &release);
    if (toggles != 0 || !opened)
        return 63;
    delete interaction;
    QDir().mkpath(folder);
    events(80);
    Splash splash;
    splash.setProgress(38);
    splash.grab().save(QDir(folder).filePath(QStringLiteral("startup-loading.png")));
    beginScan();
    for (int i = 0; i < 500 && m_engine->isBusy(); ++i)
        events(40);
    if (!m_catalog->nativeSnapshot().generation)
        return 51;
    m_catalog->selectAll(false);
    // Regression: Select recommended must also replace a prior manual remote
    // downgrade, rather than retaining it alongside newly recommended items.
    for (const auto &entry : m_catalog->entries())
    {
        bool seeded = false;
        for (int index = 1; index < entry.candidates.size(); ++index)
            if (entry.candidates[index].statusBits & 0x80u)
            {
                m_remote.insert(entry.device.instanceId);
                m_remoteChoices[entry.device.instanceId] = index;
                seeded = true;
                break;
            }
        if (seeded)
            break;
    }
    selectRecommended();
    for (const auto &entry : m_catalog->entries())
        if (entry.checked && entry.chosenCandidate > 0 &&
            !(entry.candidates[entry.chosenCandidate].statusBits & (0x10u | 0x20u)))
            return 128;
    for (const auto &entry : m_catalog->entries())
        if (m_remote.contains(entry.device.instanceId))
        {
            const int selected = m_remoteChoices.value(entry.device.instanceId, -1);
            if (selected < 1 || !(entry.candidates[selected].statusBits & (0x10u | 0x20u)))
                return 129;
        }
    m_remote.clear();
    m_remoteChoices.clear();
    m_catalog->selectAll(false);
    DriverEntry sameVersion;
    sameVersion.installedDriverPresent = true;
    sameVersion.device.driverVersion = QStringLiteral("1.2.3.4");
    sameVersion.candidates.resize(2);
    sameVersion.candidates[1].version = QStringLiteral("1.2.3.4");
    sameVersion.candidates[1].valid = sameVersion.candidates[1].local = true;
    if (reinstallCandidateIndex(sameVersion) != 1)
        return 130;
    sameVersion.candidates[1].version = QStringLiteral("1.2.3.3");
    if (isReinstallCandidate(sameVersion, 1))
        return 131;
    if (qgetenv("DRIVERPACK_MODERN_SELECTION_PROBE") == QByteArray("1"))
    {
        QJsonObject report;
        report.insert(QStringLiteral("automaticMissingUpgradeOnly"), true);
        report.insert(QStringLiteral("manualRemoteDowngradeReplaced"), true);
        report.insert(QStringLiteral("exactVersionReinstallVerified"), true);
        report.insert(QStringLiteral("installationRequested"), false);
        QFile proof(QDir(folder).filePath(QStringLiteral("selection-checks.json")));
        if (!proof.open(QIODevice::WriteOnly))
            return 134;
        proof.write(QJsonDocument(report).toJson());
        return 0;
    }
    for (int row = 0; row < m_catalog->size(); ++row)
        if (m_catalog->canQueue(row))
        {
            setDriver(row, true);
            if (m_catalog->selectedDrivers() != 1)
                return 55;
            auto plan = m_catalog->buildPlan(false, true, true);
            try
            {
                sdiomodern::validatePlan(m_catalog->nativeSnapshot(), plan);
            }
            catch (...)
            {
                return 56;
            }
            QElapsedTimer selectionClock;
            selectionClock.start();
            for (int n = 0; n < 30; ++n)
            {
                setDriver(row, false);
                setDriver(row, true);
            }
            const double selectionAverageMs = selectionClock.nsecsElapsed() / 60000000.0;
            expert(1);
            expert(0);
            const bool wasCompact = m_side->width() < 100;
            toggleSidebar();
            events(320);
            if ((m_side->width() < 100) == wasCompact)
                return 58;
            toggleSidebar();
            events(320);
            if ((m_side->width() < 100) != wasCompact || !m_nav.value(0)->isChecked())
                return 59;
            QFile timing(QDir(folder).filePath(QStringLiteral("interaction-check.json")));
            if (timing.open(QIODevice::WriteOnly))
            {
                QJsonObject report;
                report.insert(QStringLiteral("selectionAverageMs"), selectionAverageMs);
                report.insert(QStringLiteral("sidebarRoundTrip"), true);
                report.insert(QStringLiteral("installationRequested"), false);
                timing.write(QJsonDocument(report).toJson());
            }
            if (m_catalog->selectedDrivers() != 1)
                return 57;
            setDriver(row, false);
            break;
        }
    expert(0);
    beginScan();
    events(20);
    if (m_engine->isBusy())
    {
        auto overlay =
            m_pages->findChild<QWidget *>(QStringLiteral("contentLoader"), Qt::FindDirectChildrenOnly);
        if (!overlay)
            return 76;
        grab().save(QDir(folder).filePath(QStringLiteral("matching-loading.png")));
    }
    for (int n = 0; n < 500 && m_engine->isBusy(); ++n)
        events(40);
    if (m_engine->isBusy() || !m_catalog->nativeSnapshot().generation)
        return 77;
    if (UiLanguage::available().size() < 40)
        return 64;
    QJsonArray localeChecks;
    expert(5);
    for (const auto &locale : UiLanguage::available())
    {
        auto selector = findChild<QComboBox *>(QStringLiteral("applicationLanguage"));
        if (!selector)
            return 116;
        auto popup = selector->findChild<QWidget *>(QStringLiteral("languagePopup"));
        auto choices = selector->findChild<QListWidget *>(QStringLiteral("languageChoices"));
        if (!popup || !choices)
            return 117;
        popup->setAttribute(Qt::WA_DontShowOnScreen);
        selector->showPopup();
        const int index = selector->findData(locale.first);
        if (index < 0 || index >= choices->count())
            return 118;
        choices->scrollToItem(choices->item(index));
        events(30);
        const QPoint point = choices->visualItemRect(choices->item(index)).center();
        QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(choices->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(choices->viewport(), &release);
        events(60);
        if (m_language != locale.first || UiLanguage::language() != locale.first ||
            m_settings->value(QStringLiteral("language")).toString() != locale.first ||
            !UiFont::embeddedFontsAvailable() || QFontInfo(qApp->font()).family() != UiFont::family() ||
            layoutDirection() != (UiLanguage::isRtl() ? Qt::RightToLeft : Qt::LeftToRight))
            return 119;
        if (locale.first == QStringLiteral("arabic") &&
            (UiLanguage::text(QStringLiteral("Settings")) != QString::fromUtf8("الإعدادات") ||
             UiLanguage::text(QStringLiteral("Diagnostics")) != QString::fromUtf8("تشخيص الجهاز")))
            return 123;
        QJsonObject proof;
        proof.insert(QStringLiteral("locale"), locale.first);
        proof.insert(QStringLiteral("font"), QFontInfo(qApp->font()).family());
        proof.insert(QStringLiteral("settingsLabel"), UiLanguage::text(QStringLiteral("Settings")));
        proof.insert(QStringLiteral("popupMouseSelection"), true);
        localeChecks.append(proof);
    }
    QFile localeProof(QDir(folder).filePath(QStringLiteral("language-selection-checks.json")));
    if (!localeProof.open(QIODevice::WriteOnly))
        return 120;
    localeProof.write(QJsonDocument(localeChecks).toJson());
    applyLanguage(QStringLiteral("german"));
    expert(5);
    events(120);
    auto languages = findChild<QComboBox *>(QStringLiteral("applicationLanguage"));
    if (!languages || languages->count() != UiLanguage::available().size() ||
        languages->currentData().toString() != QStringLiteral("german") ||
        m_settings->value(QStringLiteral("language")).toString() != QStringLiteral("german"))
        return 65;
    if (UiLanguage::text(QStringLiteral("Settings")) == QStringLiteral("Settings"))
        return 66;
    grab().save(QDir(folder).filePath(QStringLiteral("german-settings.png")));
    if (auto popup = languages->findChild<QWidget *>(QStringLiteral("languagePopup")))
    {
        popup->setAttribute(Qt::WA_DontShowOnScreen);
        languages->showPopup();
        events(100);
        popup->grab().save(QDir(folder).filePath(QStringLiteral("language-selector.png")));
        languages->hidePopup();
    }
    applyLanguage(QStringLiteral("farsi"));
    if (layoutDirection() != Qt::RightToLeft)
        return 67;
    applyLanguage(QStringLiteral("english"));
    int first = -1;
    for (int i = 0; i < m_catalog->size(); ++i)
        if (m_catalog->entryRef(i).candidates.size() > 1)
        {
            first = i;
            break;
        }
    for (int locale = 0; locale < 2; ++locale)
    {
        setLanguage(locale == 1);
        for (int dark = 0; dark < 2; ++dark)
        {
            setDark(dark ? QStringLiteral("dark") : QStringLiteral("light"));
            const QString root =
                QDir(folder).filePath(QString(locale ? QStringLiteral("ar") : QStringLiteral("en")) +
                                      (dark ? QStringLiteral("-dark") : QStringLiteral("-light")));
            QDir().mkpath(root);
            startPage();
            if (!m_start->findChildren<QSlider *>().isEmpty())
                return 60;
            m_root->setCurrentWidget(m_start);
            events(100);
            if (!grab().save(QDir(root).filePath(QStringLiteral("start.png"))))
                return 52;
            auto signature = m_start->findChild<QWidget *>(QStringLiteral("designerCapsule"));
            if (!signature || !signature->findChild<QWidget *>(QStringLiteral("signatureHeart")) ||
                AboutMotion::heartbeatScale(100) <= 1 || AboutMotion::heartbeatScale(400) <= 1 ||
                AboutMotion::heartbeatScale(1000) != 1)
                return 115;
            signature->grab().save(QDir(root).filePath(QStringLiteral("start-signature.png")));

            const char *names[] = {"drivers", "software", "protection", "diagnostics",
                                   "offline", "settings", "about"};
            for (int page = 0; page < 7; ++page)
            {
                expert(page);
                if (page == 0)
                {
                    if (!findChildren<QWidget *>(QStringLiteral("referenceDriverPlaceholder")).isEmpty())
                        return 124;
                    for (const QString name :
                         {QStringLiteral("showInstalledToggle"), QStringLiteral("showAdditionalToggle")})
                    {
                        for (int direction = 0; direction < 2; ++direction)
                        {
                            auto toggle = dynamic_cast<DrpSwitch *>(
                                m_pages->currentWidget()->findChild<QCheckBox *>(name));
                            if (!toggle)
                                return 104;
                            if (toggle->isAnimating() || !toggle->findChild<ToggleSwitch *>())
                                return 140;
                            const bool before = toggle->isChecked();
                            QPointer<DrpSwitch> alive(toggle);
                            QElapsedTimer gestureClock;
                            gestureClock.start();
                            toggle->click();
                            if (toggle->isChecked() == before ||
                                (!qApp->property("reduceMotion").toBool() && !toggle->isAnimating()))
                                return 105;
                            events(60);
                            if (!alive && gestureClock.elapsed() < 175)
                                return 106;
                            events(240);
                        }
                    }
                }
                if (page == 5)
                {
                    auto tabs =
                        m_pages->currentWidget()->findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
                    if (!tabs || tabs->count() != 4)
                        return 74;
                    for (auto button : m_pages->currentWidget()->findChildren<QPushButton *>())
                        if (dynamic_cast<HeroButton *>(button))
                        {
                            QEvent leave(QEvent::Leave);
                            QApplication::sendEvent(button, &leave);
                            events(180);
                            const QImage before = button->grab().toImage();
                            QEvent enter(QEvent::Enter);
                            QApplication::sendEvent(button, &enter);
                            events(180);
                            if (button->grab().toImage() == before)
                                return 107;
                            grab().save(QDir(root).filePath(QStringLiteral("settings-hero-hover.png")));
                            QApplication::sendEvent(button, &leave);
                            break;
                        }

                    events(100);
                    auto fullBar = static_cast<SettingsTabs *>(tabs)->navigation();
                    if (qAbs(fullBar->width() - tabs->width()) > 2 ||
                        fullBar->tabArea(0).united(fullBar->tabArea(tabs->count() - 1)).width() <
                            tabs->width() - 3)
                        return 92;
                    const QPoint tabPoint = fullBar->tabArea(1).center();
                    QMouseEvent tabPress(QEvent::MouseButtonPress, tabPoint, Qt::LeftButton, Qt::LeftButton,
                                         Qt::NoModifier);
                    QApplication::sendEvent(fullBar, &tabPress);
                    if (tabs->currentIndex() != 1 ||
                        (!qApp->property("reduceMotion").toBool() && !fullBar->isTransitioning()))
                        return 99;
                    QMouseEvent tabRelease(QEvent::MouseButtonRelease, tabPoint, Qt::LeftButton, Qt::NoButton,
                                           Qt::NoModifier);
                    QApplication::sendEvent(fullBar, &tabRelease);
                    events(240);
                    tabs->setCurrentIndex(0);
                    events(240);
                    for (int index = 0; index < tabs->count(); ++index)
                    {
                        tabs->setCurrentIndex(index);
                        if (index == 1)
                        {
                            events(60);
                            grab().save(QDir(root).filePath(QStringLiteral("settings-tab-transition.png")));
                        }
                        events(240);
                        grab().save(
                            QDir(root).filePath(QStringLiteral("settings-category-%1.png").arg(index)));
                    }
                    tabs->setCurrentIndex(0);
                }
                if (page == 1)
                {
                    auto group = m_pages->currentWidget()->findChild<QCheckBox *>(
                        QStringLiteral("softwareGroupToggle"));
                    if (!group || group->property("softwareIds").toStringList().isEmpty())
                        return 135;
                    const auto ids = group->property("softwareIds").toStringList();
                    for (int cycle = 0; cycle < 3; ++cycle)
                    {
                        group->click();
                        for (const auto &id : ids)
                            if (!m_programSelection.contains(id))
                                return 136;
                        group->click();
                        for (const auto &id : ids)
                            if (m_programSelection.contains(id))
                                return 137;
                    }
                    for (auto tick : m_pages->currentWidget()->findChildren<QCheckBox *>())
                        if (!tick->property("softwareId").toString().isEmpty() && tick->isEnabled())
                        {
                            auto row = tick->parentWidget();
                            row->setAttribute(Qt::WA_UnderMouse, false);
                            const QImage before = row->grab().toImage();
                            row->setAttribute(Qt::WA_UnderMouse, true);
                            QHoverEvent hover(QEvent::HoverEnter, QPointF(20, 20), QPointF(-1, -1));
                            QApplication::sendEvent(row, &hover);
                            events(60);
                            if (row->grab().toImage() == before)
                                return 138;
                            grab().save(QDir(root).filePath(QStringLiteral("software-row-hover.png")));
                            row->setAttribute(Qt::WA_UnderMouse, false);
                            QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(20, 20));
                            QApplication::sendEvent(row, &leave);
                            break;
                        }
                    const auto icons =
                        m_pages->currentWidget()->findChildren<QLabel *>(QStringLiteral("programIcon"));
                    if (icons.isEmpty())
                        return 132;
                    for (auto icon : icons)
                        if (!icon->pixmap() || icon->pixmap()->isNull())
                            return 133;
                    if (!m_softwarePrimary || !m_softwarePrimary->isEnabled())
                        return 80;
                    auto add =
                        m_pages->currentWidget()->findChild<QPushButton *>(QStringLiteral("addSoftware"));
                    if (!add)
                        return 81;
                    QTimer::singleShot(100, this,
                                       [root]()
                                       {
                                           for (auto popup : QApplication::topLevelWidgets())
                                               if (popup->objectName() == QStringLiteral("addProgramDialog"))
                                               {
                                                   popup->grab().save(QDir(root).filePath(
                                                       QStringLiteral("add-program.png")));
                                                   if (auto dialog = qobject_cast<QDialog *>(popup))
                                                       dialog->reject();
                                               }
                                       });
                    add->click();
                    QPushButton *editor = 0;
                    for (auto candidate : m_pages->currentWidget()->findChildren<QPushButton *>())
                        if (!candidate->property("editSoftwareId").toString().isEmpty())
                        {
                            editor = candidate;
                            break;
                        }
                    if (!editor)
                        return 149;
                    QTimer::singleShot(
                        130, this,
                        [root]()
                        {
                            for (auto popup : QApplication::topLevelWidgets())
                                if (popup->objectName() == QStringLiteral("addProgramDialog"))
                                {
                                    auto name =
                                        popup->findChild<QLineEdit *>(QStringLiteral("customProgramName"));
                                    if (!name || name->text().isEmpty())
                                        qFatal("Editor did not populate existing software metadata");
                                    popup->grab().save(
                                        QDir(root).filePath(QStringLiteral("edit-program.png")));
                                    if (auto dialog = qobject_cast<QDialog *>(popup))
                                        dialog->reject();
                                }
                        });
                    const QImage beforeEditor = editor->grab().toImage();
                    editor->setAttribute(Qt::WA_UnderMouse, true);
                    QEvent editorEnter(QEvent::Enter);
                    QApplication::sendEvent(editor, &editorEnter);
                    events(70);
                    if (editor->grab().toImage() == beforeEditor)
                        return 150;
                    grab().save(QDir(root).filePath(QStringLiteral("software-pencil-hover.png")));
                    editor->setAttribute(Qt::WA_UnderMouse, false);
                    QEvent editorLeave(QEvent::Leave);
                    QApplication::sendEvent(editor, &editorLeave);
                    editor->click();
                }
                if (page == 3)
                {
                    if (m_hardwareData.isEmpty() && !m_hardware->isRunning())
                        refreshHardware();
                    if (m_hardware->isRunning())
                    {
                        expert(3);
                        events(40);
                        if (m_pages->currentWidget()->findChild<QWidget *>(QStringLiteral("contentLoader")))
                            grab().save(QDir(root).filePath(QStringLiteral("diagnostics-loading.png")));
                    }
                    for (int i = 0; i < 230 && m_hardware->isRunning(); ++i)
                        events(100);
                    expert(3);
                    events(100);
                    QWidget *board = 0, *bios = 0, *network = 0, *display = 0;
                    for (auto card : m_pages->currentWidget()->findChildren<QWidget *>())
                    {
                        const QString klass = card->property("diagnosticClass").toString();
                        if (!klass.isEmpty() && card->isVisible() && card->width() <= 100)
                            return 70;
                        if (klass == QStringLiteral("Win32_BaseBoard"))
                            board = card;
                        if (klass == QStringLiteral("Win32_BIOS"))
                            bios = card;
                        if (klass == QStringLiteral("Win32_VideoController"))
                            display = card;
                        if (klass == QStringLiteral("Win32_NetworkAdapter"))
                            network = card;
                    }
                    auto navigation = m_pages->currentWidget()->findChild<SettingsTabs *>(
                        QStringLiteral("diagnosticNavigation"));
                    auto sections = navigation;
                    if (!navigation || !sections || navigation->count() != 6)
                        return 78;
                    if (qAbs(navigation->navigation()->width() - navigation->width()) > 2 ||
                        navigation->navigation()
                                ->tabArea(0)
                                .united(navigation->navigation()->tabArea(5))
                                .width() < navigation->width() - 3)
                        return 93;
                    navigation->setCurrentIndex(0);
                    events(240);
                    auto scroller = sections->currentWidget()->findChild<QScrollArea *>();
                    if (board && bios && scroller)
                    {
                        if (board->parentWidget()->width() >= 860 &&
                            board->geometry().top() != bios->geometry().top())
                            return 68;
                        scroller->ensureWidgetVisible(board, 0, 30);
                        events(80);
                        grab().save(QDir(root).filePath(QStringLiteral("diagnostics-board-bios.png")));
                    }
                    if (display && scroller)
                    {
                        if (!display->findChild<QTreeWidget *>(QStringLiteral("diagnosticRecordTable")))
                            return 75;
                        navigation->setCurrentIndex(1);
                        events(80);
                        auto displayScroll = sections->currentWidget()->findChild<QScrollArea *>();
                        displayScroll->ensureWidgetVisible(display, 0, 30);
                        events(80);
                        grab().save(QDir(root).filePath(QStringLiteral("diagnostics-display.png")));
                    }
                    navigation->setCurrentIndex(0);
                    events(80);
                    if (network && scroller)
                    {
                        if (!network->findChild<QTreeWidget *>(QStringLiteral("diagnosticRecordTable")))
                            return 69;
                        scroller->ensureWidgetVisible(network, 0, 30);
                        events(80);
                        grab().save(QDir(root).filePath(QStringLiteral("diagnostics-network.png")));
                    }
                    if (scroller)
                    {
                        scroller->verticalScrollBar()->setValue(0);
                        events(100);
                        auto bar = scroller->verticalScrollBar();
                        const QRect thumb = bar->property("paintedThumb").toRect();
                        if (bar->maximum() > 0 && !thumb.isEmpty())
                        {
                            const int before = bar->value();
                            const QPoint start = thumb.center(), end = start + QPoint(0, 45);
                            QMouseEvent press(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
                                              Qt::NoModifier);
                            QApplication::sendEvent(bar, &press);
                            QMouseEvent move(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton,
                                             Qt::NoModifier);
                            QApplication::sendEvent(bar, &move);
                            QMouseEvent release(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton,
                                                Qt::NoModifier);
                            QApplication::sendEvent(bar, &release);
                            if (bar->value() <= before)
                                return 79;
                            ++dragChecks;
                            bar->setValue(0);
                        }
                    }
                    for (int n = 0; n < navigation->count(); ++n)
                    {
                        navigation->setCurrentIndex(n);
                        events(240);
                        grab().save(QDir(root).filePath(QStringLiteral("diagnostics-section-%1.png").arg(n)));
                    }
                    navigation->setCurrentIndex(0);
                    events(240);
                    auto tabBar = navigation->navigation();
                    const QPoint point = tabBar->tabArea(2).center();
                    QHelpEvent help(QEvent::ToolTip, point, tabBar->mapToGlobal(point));
                    QApplication::sendEvent(tabBar, &help);
                    events(60);
                    bool customTip = false;
                    for (auto popup : QApplication::topLevelWidgets())
                        if (popup->objectName() == QStringLiteral("projectTooltip") && popup->isVisible())
                        {
                            customTip = true;
                            popup->grab().save(
                                QDir(root).filePath(QStringLiteral("diagnostics-tab-tooltip.png")));
                            popup->hide();
                        }
                    if (!customTip)
                        return 103;
                }
                if (page == 2 || page == 3)
                {
                    for (auto table : m_pages->currentWidget()->findChildren<QTreeWidget *>())
                        if (table->topLevelItemCount())
                        {
                            table->topLevelItem(0)->setSelected(true);
                            break;
                        }
                }
                if (page == 2)
                {
                    for (auto table : m_pages->currentWidget()->findChildren<QTreeWidget *>())
                        if (table->topLevelItemCount())
                        {
                            auto item = table->topLevelItem(0);
                            table->scrollToItem(item);
                            events(40);
                            const QPoint point(table->visualItemRect(item).left() + 40,
                                               table->visualItemRect(item).center().y());
                            const QString before = table->indexAt(point).data().toString();
                            QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton,
                                              Qt::NoModifier);
                            QApplication::sendEvent(table->viewport(), &press);
                            QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton,
                                                Qt::NoButton, Qt::NoModifier);
                            QApplication::sendEvent(table->viewport(), &release);
                            QMouseEvent doubleClick(QEvent::MouseButtonDblClick, point, Qt::LeftButton,
                                                    Qt::LeftButton, Qt::NoModifier);
                            QApplication::sendEvent(table->viewport(), &doubleClick);
                            events(60);
                            for (auto editor : table->findChildren<QLineEdit *>())
                                if (editor->isVisible())
                                    return 82;
                            if (QApplication::clipboard()->text() != before)
                                return 83;
                            QKeyEvent copyEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
                            QApplication::sendEvent(table, &copyEvent);
                            if (QApplication::clipboard()->text() != before)
                                return 88;
                            ++cellChecks;
                            break;
                        }
                }
                for (auto box : m_pages->currentWidget()->findChildren<QCheckBox *>())
                    if (auto toggle = dynamic_cast<DrpSwitch *>(box))
                    {
                        if (toggle->isAnimating())
                            return 101;
                    }
                events(340);
                if (!grab().save(
                        QDir(root).filePath(QString::fromLatin1(names[page]) + QStringLiteral(".png"))))
                    return 53;
            }
            if (auto bar = findChild<QWidget *>(QStringLiteral("customTitleBar")))
            {
                for (auto button : bar->findChildren<QToolButton *>())
                    if (button->isVisible() && !button->toolTip().isEmpty())
                    {
                        QHelpEvent help(QEvent::ToolTip, button->rect().center(),
                                        button->mapToGlobal(button->rect().center()));
                        QApplication::sendEvent(button, &help);
                        events(60);
                        for (auto popup : QApplication::topLevelWidgets())
                            if (popup->objectName() == QStringLiteral("projectTooltip"))
                            {
                                if (!frameGeometry().contains(popup->frameGeometry()))
                                    return 84;
                                popup->grab().save(QDir(root).filePath(QStringLiteral("tooltip.png")));
                                popup->hide();
                            }
                        break;
                    }
            }
            expert(3);
            events(80);
            m_additional = true;
            expert(0);
            events(80);
            if (auto picker =
                    m_pages->currentWidget()->findChild<QPushButton *>(QStringLiteral("driverFieldPicker")))
            {
                QTimer::singleShot(100, this,
                                   [root]()
                                   {
                                       for (auto popup : QApplication::topLevelWidgets())
                                           if (popup->objectName() == QStringLiteral("driverFieldPanel"))
                                           {
                                               popup->grab().save(
                                                   QDir(root).filePath(QStringLiteral("driver-fields.png")));
                                               if (auto dialog = qobject_cast<QDialog *>(popup))
                                                   dialog->reject();
                                           }
                                   });
                picker->click();
            }
            m_additional = false;
            QTimer::singleShot(
                100, this,
                [this, root]()
                {
                    for (QWidget *widget : QApplication::topLevelWidgets())
                        if (auto dialog = qobject_cast<QDialog *>(widget))
                            if (dialog->findChild<QPlainTextEdit *>(QStringLiteral("bugDescription")))
                            {
                                dialog->grab().save(QDir(root).filePath(QStringLiteral("bug-report.png")));
                                dialog->reject();
                            }
                });
            reportDialog();
            expert(0);
            tools();
            events(300);
            grab().save(QDir(root).filePath(QStringLiteral("tools.png")));
            for (auto tool : m_tools->findChildren<QToolButton *>())
                if (tool->objectName() == QStringLiteral("toolsDrawerRow") && tool->isEnabled())
                {
                    const QImage before = tool->grab().toImage();
                    tool->setAttribute(Qt::WA_UnderMouse, true);
                    QEvent enter(QEvent::Enter);
                    QApplication::sendEvent(tool, &enter);
                    events(80);
                    if (tool->grab().toImage() == before)
                        return 151;
                    grab().save(QDir(root).filePath(QStringLiteral("tools-hover.png")));
                    tool->setAttribute(Qt::WA_UnderMouse, false);
                    QEvent leave(QEvent::Leave);
                    QApplication::sendEvent(tool, &leave);
                    break;
                }
            m_tools->closeDrawer();
            events(300);
            if (findChild<QWidget *>(QStringLiteral("driverTree")) ||
                findChild<QWidget *>(QStringLiteral("driverWorkspace")))
                return 54;
            if (first >= 0)
            {
                auto detail = detailDialog(first);
                detail->setAttribute(Qt::WA_DontShowOnScreen);
                detail->show();
                events(80);
                detail->grab().save(QDir(root).filePath(QStringLiteral("driver-information.png")));
                delete detail;
            }
            DrpTransferDialog transfers(m_library, neededPackages(), m_arabic, this);
            transfers.setAttribute(Qt::WA_DontShowOnScreen);
            transfers.show();
            events(80);
            transfers.grab().save(QDir(root).filePath(QStringLiteral("downloads.png")));
            auto check = transfers.findChild<QPushButton *>(QStringLiteral("checkTransferUpdates"));
            auto transferWorker = transfers.findChild<OnlineWorker *>();
            if (!check || !check->property("projectAction").toBool() || !transferWorker)
                return 94;
            check->click();
            events(80);
            if (check->isEnabled())
                return 95;
            transfers.grab().save(QDir(root).filePath(QStringLiteral("downloads-checking.png")));
            transferWorker->error = QStringLiteral("Preview network failure — no network request was made.");
            QMetaObject::invokeMethod(transferWorker, "finished", Qt::DirectConnection);
            events(80);
            if (!check->isEnabled())
                return 96;
            auto transferStatus = transfers.findChild<QLabel *>(QStringLiteral("transferStatus"));
            if (!transferStatus ||
                transferStatus->property("transferState").toString() != QStringLiteral("error"))
                return 97;
            transfers.grab().save(QDir(root).filePath(QStringLiteral("downloads-error-preview.png")));
            transferWorker->error.clear();
            QMetaObject::invokeMethod(transferWorker, "finished", Qt::DirectConnection);
            events(80);
            if (transferStatus->property("transferState").toString() != QStringLiteral("ready"))
                return 98;
            transfers.grab().save(QDir(root).filePath(QStringLiteral("downloads-ready.png")));
            if (auto records = transfers.findChild<QTreeWidget *>())
            {
                if (records->topLevelItemCount())
                {
                    auto row = records->topLevelItem(0);
                    const auto original = row->checkState(0);
                    for (int side = 0; side < 2; ++side)
                    {
                        const QPoint point(side ? 65 : records->viewport()->width() - 24,
                                           records->visualItemRect(row).center().y());
                        QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton,
                                          Qt::NoModifier);
                        QApplication::sendEvent(records->viewport(), &press);
                        events(30);
                        if (row->checkState(0) !=
                            (side ? original : (original == Qt::Checked ? Qt::Unchecked : Qt::Checked)))
                            return 102;
                    }
                    records->viewport()->update();
                    events(30);
                    transfers.grab().save(QDir(root).filePath(QStringLiteral("downloads-full-row.png")));
                }
            }
            if (!transfers.previewPauseCycle(root))
                return 100;
            transfers.close();
            if (first >= 0)
            {
                m_showInstalled = true;
                m_expanded.insert(m_catalog->entryRef(first).device.instanceId);
                expert(0);
                events(100);
                auto group =
                    m_pages->currentWidget()->findChild<QCheckBox *>(QStringLiteral("driverGroupToggle"));
                if (!group || !group->isEnabled())
                    return 108;
                QWidget *originalPage = m_pages->currentWidget();
                int modelNotices = 0;
                const auto notice =
                    connect(m_catalog, &DriverCatalog::changed, this, [&modelNotices]() { ++modelNotices; });
                QElapsedTimer batchClock;
                batchClock.start();
                for (int cycle = 0; cycle < 12; ++cycle)
                {
                    group->click();
                    if (group->checkState() != Qt::Checked ||
                        m_catalog->selectedDrivers() + m_remote.size() == 0 ||
                        m_pages->currentWidget() != originalPage)
                        return 109;
                    group->click();
                    if (group->checkState() != Qt::Unchecked ||
                        m_catalog->selectedDrivers() + m_remote.size() != 0 ||
                        m_pages->currentWidget() != originalPage)
                        return 110;
                }
                disconnect(notice);
                if (modelNotices != 0)
                    return 111;
                int available = 0, firstAvailable = -1;
                for (const auto &value : group->property("groupRows").toList())
                    if (groupCandidate(m_catalog, value.toInt()) >= 0)
                    {
                        ++available;
                        if (firstAvailable < 0)
                            firstAvailable = value.toInt();
                    }
                if (available > 1)
                {
                    setDriver(firstAvailable, true);
                    if (group->checkState() != Qt::PartiallyChecked)
                        return 112;
                    group->click();
                    if (group->checkState() != Qt::Checked)
                        return 113;
                    group->click();
                    if (group->checkState() != Qt::Unchecked)
                        return 114;
                }
                QFile groupProof(QDir(root).filePath(QStringLiteral("group-selection-checks.json")));
                if (groupProof.open(QIODevice::WriteOnly))
                {
                    QJsonObject check;
                    check.insert(QStringLiteral("selectClearCycles"), 12);
                    check.insert(QStringLiteral("perRowModelNotifications"), modelNotices);
                    check.insert(QStringLiteral("pageRebuilt"), false);
                    check.insert(QStringLiteral("availableRows"), available);
                    check.insert(QStringLiteral("elapsedMs"), double(batchClock.elapsed()));
                    groupProof.write(QJsonDocument(check).toJson());
                }

                const QString key = m_catalog->entryRef(first).device.instanceId;
                for (auto button : m_pages->currentWidget()->findChildren<QAbstractButton *>(
                         QStringLiteral("driverChevron")))
                    if (button->property("driverKey").toString() == key)
                    {
                        button->click();
                        events(250);
                        if (m_expanded.contains(key))
                            return 85;
                        button->click();
                        auto panel = dynamic_cast<DriverAlternatives *>(
                            button->parentWidget()->parentWidget()->findChild<QWidget *>(
                                QStringLiteral("driverAlternatives")));
                        if (!panel)
                            return 121;
                        int previousHeight = panel->height();
                        const QPoint anchor = button->pos();
                        for (int frame = 0; frame < 12; ++frame)
                        {
                            events(20);
                            if (panel->height() < previousHeight || button->pos() != anchor)
                            {
                                QJsonObject failure;
                                failure.insert(QStringLiteral("frame"), frame);
                                failure.insert(QStringLiteral("height"), panel->height());
                                failure.insert(QStringLiteral("previousHeight"), previousHeight);
                                failure.insert(QStringLiteral("width"), panel->width());
                                failure.insert(QStringLiteral("headerMoved"), button->pos() != anchor);
                                failure.insert(QStringLiteral("beforeX"), anchor.x());
                                failure.insert(QStringLiteral("beforeY"), anchor.y());
                                failure.insert(QStringLiteral("afterX"), button->pos().x());
                                failure.insert(QStringLiteral("afterY"), button->pos().y());
                                QFile diagnostic(
                                    QDir(root).filePath(QStringLiteral("expansion-failure.json")));
                                if (diagnostic.open(QIODevice::WriteOnly))
                                    diagnostic.write(QJsonDocument(failure).toJson());
                                return 122;
                            }
                            previousHeight = panel->height();
                        }
                        grab().save(QDir(root).filePath(QStringLiteral("driver-expanding.png")));
                        auto stableRow = button->parentWidget();
                        const auto stableLabels = stableRow->findChildren<QLabel *>();
                        QVector<QRect> anchors;
                        for (auto label : stableLabels)
                            anchors << label->geometry();
                        const int stableHeight = stableRow->height();
                        button->click();
                        int closingHeight = panel->height();
                        for (int frame = 0; frame < 12; ++frame)
                        {
                            events(20);
                            if (panel->height() > closingHeight || stableRow->height() != stableHeight)
                                return 152;
                            for (int n = 0; n < stableLabels.size(); ++n)
                                if (stableLabels[n]->geometry() != anchors[n])
                                    return 153;
                            closingHeight = panel->height();
                        }
                        button->click();
                        events(250);
                        for (int reversal = 0; reversal < 6; ++reversal)
                        {
                            button->click();
                            events(35);
                        }
                        events(250);
                        if (!m_expanded.contains(key))
                            return 86;
                        ++expansionChecks;
                        break;
                    }
                QVector<QAbstractButton *> versions;
                for (auto tick : m_pages->currentWidget()->findChildren<QAbstractButton *>())
                    if (tick->property("driverRowIndex").isValid() &&
                        tick->property("driverRowIndex").toInt() == first &&
                        tick->property("driverCandidateIndex").toInt() > 0 && tick->isEnabled())
                        versions << tick;
                if (versions.size() > 1)
                {
                    versions[1]->setChecked(true);
                    versions[0]->setChecked(true);
                    for (auto tick : versions)
                        if (tick->parentWidget()->property("selectedDriver").toBool() != tick->isChecked())
                            return 91;
                    setDriver(first, false);
                }
                grab().save(QDir(root).filePath(QStringLiteral("driver-alternatives.png")));
                m_showInstalled = false;
                m_expanded.clear();
            }
            m_resultNames = QStringList()
                            << t("Layout preview — no installation started", "معاينة الشكل — لم يبدأ تثبيت");
            m_resultRows.clear();
            m_resultRows.insert(QStringLiteral("preview"), 0);
            m_resultStates.clear();
            installationPage();
            m_clock->stop();
            m_installBar->setRange(0, 100);
            m_installBar->setValue(0);
            m_installText->setText(t("Interface preview only. No installation was performed.",
                                     "معاينة الواجهة فقط. لم يُنفّذ أي تثبيت."));
            events(80);
            grab().save(QDir(root).filePath(QStringLiteral("installation-preview.png")));
            m_resultFailed = false;
            m_error.clear();
            finalPage();
            events(80);
            grab().save(QDir(root).filePath(QStringLiteral("result-preview.png")));
        }
    }
    if (dragChecks < 4 || cellChecks < 4 || expansionChecks < 4)
        return 89;
    QFile proof(QDir(folder).filePath(QStringLiteral("ui-behavior-checks.json")));
    if (proof.open(QIODevice::WriteOnly))
    {
        QJsonObject report;
        report.insert(QStringLiteral("nativeThumbDrags"), dragChecks);
        report.insert(QStringLiteral("copiedCellValues"), cellChecks);
        report.insert(QStringLiteral("driverExpansionCycles"), expansionChecks);
        report.insert(QStringLiteral("nativeFrameRoundTrips"), nativeFrameChecks);
        report.insert(QStringLiteral("referenceOnlyRows"),
                      findChildren<QWidget *>(QStringLiteral("referenceDriverPlaceholder")).size());
        report.insert(QStringLiteral("popupLanguagesChecked"), localeChecks.size());
        report.insert(QStringLiteral("installationRequested"), false);
        proof.write(QJsonDocument(report).toJson());
    }
    return 0;
}

int DriverPackShell::gallery(const QString &folder) {
    if(!m_demo||!m_capture||qgetenv("DRIVERPACK_MODERN_TEST_SETTINGS").isEmpty())return 50;
    setAttribute(Qt::WA_DontShowOnScreen);resize(1360,850);show();m_initial=false;m_model=QStringLiteral("Demo workstation");m_hardwareData=galleryHardware();
    sdiomodern::Snapshot demo;demo.generation=1;demo.sourceRevision=L"UI demonstration only";demo.capabilities.nativeCore=true;demo.capabilities.matching=true;demo.capabilities.localInstall=true;
    for(int n=0;n<3;++n){sdiomodern::Device device;device.instanceId=(QStringLiteral("DEMO\\DEVICE_%1").arg(n)).toStdWString();device.name=(n==0?QStringLiteral("Sample graphics adapter"):n==1?QStringLiteral("Sample network adapter"):QStringLiteral("Sample audio device")).toStdWString();device.manufacturer=L"Sample hardware vendor";device.category=n==0?L"Display":n==1?L"Net":L"Media";device.hasInstalledDriver=true;device.installed.version=L"1.0.0";device.installed.provider=L"Sample vendor";device.hardwareIds.push_back(device.instanceId);sdiomodern::Candidate candidate;candidate.token=100+n;candidate.version=L"2.0.0";candidate.date=L"2026-01-01";candidate.provider=L"Sample vendor";candidate.hardwareId=device.instanceId;candidate.packageName=L"Sample-package.7z";candidate.packagePath=L"Sample-package.7z";candidate.inf=L"sample.inf";candidate.section=L"DemoInstall.NT";candidate.valid=true;candidate.local=true;candidate.recommended=true;candidate.statusBits=0x10;candidate.originalOrder=0;device.candidates.push_back(candidate);demo.devices.push_back(device);}
    m_catalog->replaceNative(demo);m_catalog->setSelected(0,true);loadSoftware();
    auto snap=[&](const QString &path){static_cast<TitleBar *>(m_header)->setStatus(QStringLiteral("DEMO"));events(180);return grab().save(path);};
    for(const QString &lang:{QStringLiteral("english"),QStringLiteral("arabic")})for(const QString &mode:{QStringLiteral("light"),QStringLiteral("dark")}) {
        applyLanguage(lang);m_themeMode=mode;appearance();m_model=QStringLiteral("Demo workstation");
        const QString root=QDir(folder).filePath((lang==QStringLiteral("arabic")?QStringLiteral("ar-"):QStringLiteral("en-"))+mode);QDir().mkpath(root);
        startPage();m_root->setCurrentWidget(m_start);snap(QDir(root).filePath(QStringLiteral("01-start.png")));
        const QStringList names={QStringLiteral("02-drivers.png"),QStringLiteral("03-software.png"),QStringLiteral("04-security.png"),QStringLiteral("05-diagnostics.png"),QStringLiteral("06-offline.png"),QStringLiteral("07-settings.png"),QStringLiteral("08-about.png")};
        for(int page=0;page<7;++page){expert(page);snap(QDir(root).filePath(names[page]));}
        expert(0);tools();events(300);snap(QDir(root).filePath(QStringLiteral("09-tools.png")));m_tools->closeDrawer();events(300);
        QTimer::singleShot(400,this,[root](){for(auto widget:QApplication::topLevelWidgets())if(widget->objectName()==QStringLiteral("activityLogDialog")){widget->grab().save(QDir(root).filePath(QStringLiteral("10-activity-log.png")));if(auto dialog=qobject_cast<QDialog *>(widget))dialog->reject();}});logDialog();
        DrpTransferDialog transfer(m_library,neededPackages(),m_arabic,this);transfer.show();events(250);transfer.grab().save(QDir(root).filePath(QStringLiteral("11-downloads.png")));transfer.reject();
        m_resultNames=QStringList()<<t("Sample network driver — preview","تعريف شبكة توضيحي — معاينة");m_resultRows.clear();m_resultRows.insert(QStringLiteral("sample"),0);m_resultStates.clear();installationPage();m_clock->stop();m_installBar->setRange(0,100);m_installBar->setValue(45);m_installText->setText(t("Interface preview only. No installation was performed.","معاينة الواجهة فقط. لم يُنفّذ أي تثبيت."));snap(QDir(root).filePath(QStringLiteral("12-installation-preview.png")));
    }
    return 0;
}
