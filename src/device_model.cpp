#include "device_model.h"
#include "vector_icons.h"
#include <QPainter>
#include <QApplication>
#include <QStyle>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QKeyEvent>

DeviceModel::DeviceModel(QObject *parent) : QAbstractListModel(parent) {}
int DeviceModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_items.size(); }

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();
    const DeviceInfo &d = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole: return d.name;
    case Qt::CheckStateRole: return m_checked.at(index.row()) ? Qt::Checked : Qt::Unchecked;
    case ClassRole: return d.deviceClass;
    case ManufacturerRole: return d.manufacturer;
    case VersionRole: return d.driverVersion;
    case ProblemRole: return d.hasProblem;
    case IdRole: return d.instanceId;
    case HardwareRole: return d.hardwareId;
    case CodeRole: return d.problemCode;
    default: return QVariant();
    }
}
bool DeviceModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::CheckStateRole || !index.isValid() || index.row() >= m_checked.size())
        return false;
    const bool checked = value.toInt() == Qt::Checked;
    if (m_checked[index.row()] == checked)
        return true;
    m_checked[index.row()] = checked;
    emit dataChanged(index, index);
    emit checkedCountChanged(checkedCount());
    return true;
}
Qt::ItemFlags DeviceModel::flags(const QModelIndex &index) const
{
    return !index.isValid() ? Qt::NoItemFlags : Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
}
void DeviceModel::replace(const QVector<DeviceInfo> &devices)
{
    beginResetModel();
    m_items = devices;
    m_checked = QVector<bool>(devices.size(), false);
    endResetModel();
    emit checkedCountChanged(0);
}
int DeviceModel::problemCount() const
{
    int count = 0;
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).hasProblem) ++count;
    return count;
}
int DeviceModel::checkedCount() const
{
    int count = 0;
    for (int i = 0; i < m_checked.size(); ++i)
        if (m_checked.at(i)) ++count;
    return count;
}
DeviceInfo DeviceModel::deviceAt(int row) const
{ return row >= 0 && row < m_items.size() ? m_items.at(row) : DeviceInfo(); }
void DeviceModel::checkAll(bool value)
{
    if (m_checked.isEmpty()) return;
    for (int i = 0; i < m_checked.size(); ++i) m_checked[i] = value;
    emit dataChanged(index(0), index(m_checked.size()-1));
    emit checkedCountChanged(checkedCount());
}

DeviceFilter::DeviceFilter(QObject *parent)
  : QSortFilterProxyModel(parent), m_problemsOnly(false), m_showInstalled(true) {}
void DeviceFilter::setSearch(const QString &search) { m_search = search.trimmed(); invalidateFilter(); }
void DeviceFilter::setProblemsOnly(bool enabled) { m_problemsOnly = enabled; invalidateFilter(); }
void DeviceFilter::setShowInstalled(bool enabled) { m_showInstalled = enabled; invalidateFilter(); }
bool DeviceFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    const bool problem = sourceModel()->data(idx, DeviceModel::ProblemRole).toBool();
    if ((m_problemsOnly || !m_showInstalled) && !problem) return false;
    if (m_search.isEmpty()) return true;
    const QString searchText = sourceModel()->data(idx, Qt::DisplayRole).toString()
        + QLatin1Char(' ') + sourceModel()->data(idx, DeviceModel::ManufacturerRole).toString()
        + QLatin1Char(' ') + sourceModel()->data(idx, DeviceModel::ClassRole).toString()
        + QLatin1Char(' ') + sourceModel()->data(idx, DeviceModel::HardwareRole).toString();
    return searchText.contains(m_search, Qt::CaseInsensitive);
}

DeviceDelegate::DeviceDelegate(QObject *parent)
  : QStyledItemDelegate(parent), m_dark(false), m_details(false), m_arabic(false) {}
void DeviceDelegate::setDark(bool dark) { m_dark = dark; }
void DeviceDelegate::setDetails(bool details) { m_details = details; }
void DeviceDelegate::setArabic(bool arabic) { m_arabic = arabic; }
QSize DeviceDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{ return QSize(qMax(450, option.rect.width()), m_details ? 106 : 82); }

void DeviceDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &idx) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRect r = option.rect;
    const bool checked = idx.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    const bool problem = idx.data(DeviceModel::ProblemRole).toBool();
    const bool hovered = option.state & QStyle::State_MouseOver;
    QColor bg = m_dark ? QColor("#182D3C") : QColor("#FFFFFF");
    if (idx.row() & 1) bg = m_dark ? QColor("#1D3443") : QColor("#F7F6F4");
    if (hovered) bg = m_dark ? QColor("#25465A") : QColor("#EBF7F7");
    p->fillRect(r, bg);
    p->setPen(m_dark ? QColor("#30495B") : QColor("#E3E5E6"));
    p->drawLine(r.bottomLeft(), r.bottomRight());

    const QColor primary = m_dark ? QColor("#EFF7F8") : QColor("#183141");
    const QColor secondary = m_dark ? QColor("#ACC3CE") : QColor("#8997A2");
    const QColor iconColor = problem ? QColor("#CF8A32") : QColor("#27A9E7");
    const int pad = 24;
    const int iconX = m_arabic ? r.right()-pad-36 : r.left()+pad;
    const QRectF iconTile(iconX-6,r.center().y()-22,44,44);
    p->setPen(Qt::NoPen);
    p->setBrush(m_dark ? QColor("#204558") : QColor("#EBF6FB"));
    p->drawRoundedRect(iconTile,7,7);
    const QString deviceClass=idx.data(DeviceModel::ClassRole).toString();
    VectorIcons::paint(p,VectorIcons::forDeviceClass(deviceClass),
                       QRectF(iconX+4,r.center().y()-13,24,26),iconColor);

    const int checkWidth = 42;
    const int stateWidth = 142;
    const int rightReserved = checkWidth + stateWidth + 24;
    const int leftText = m_arabic ? r.left()+rightReserved : iconX+52;
    const int rightText = m_arabic ? iconX-20 : r.right()-rightReserved;
    const QRect titleRect(leftText, r.top()+14, qMax(0, rightText-leftText), 28);
    QFont font = option.font;
    font.setPointSize(11);
    font.setWeight(QFont::Medium);
    p->setFont(font);
    p->setPen(primary);
    const Qt::Alignment hAlign = m_arabic ? Qt::AlignRight : Qt::AlignLeft;
    p->drawText(titleRect, hAlign | Qt::AlignVCenter,
                QFontMetrics(font).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, titleRect.width()));

    font.setPointSize(9);
    font.setWeight(QFont::Normal);
    p->setFont(font);
    p->setPen(secondary);
    const QString subtitle = idx.data(DeviceModel::ManufacturerRole).toString() +
        QStringLiteral(" · ") + idx.data(DeviceModel::ClassRole).toString();
    p->drawText(QRect(titleRect.left(), r.top()+42, titleRect.width(), 22), hAlign | Qt::AlignVCenter,
                QFontMetrics(font).elidedText(subtitle, Qt::ElideRight, titleRect.width()));
    if (m_details) {
        const QString hwid = idx.data(DeviceModel::HardwareRole).toString();
        p->drawText(QRect(titleRect.left(), r.top()+67, titleRect.width(), 22), hAlign | Qt::AlignVCenter,
                    QFontMetrics(font).elidedText(hwid, Qt::ElideRight, titleRect.width()));
    }

    QRect boxRect = m_arabic ? QRect(r.left()+18, r.center().y()-10, 21, 21)
                             : QRect(r.right()-39, r.center().y()-10, 21, 21);
    p->setPen(QPen(checked ? QColor("#0CB4A9") : QColor("#A7ADB1"), 1.5));
    p->setBrush(checked ? QColor("#00B8AC") : QColor("#FFFFFF"));
    p->drawRoundedRect(boxRect, 3, 3);
    if (checked) {
        p->setPen(QPen(Qt::white, 2.0));
        QPolygon marks;
        marks << QPoint(boxRect.left()+5, boxRect.center().y())
              << QPoint(boxRect.left()+9, boxRect.bottom()-5)
              << QPoint(boxRect.right()-4, boxRect.top()+5);
        p->drawPolyline(marks);
    }

    const QRect statusRect(m_arabic ? r.left()+51 : r.right()-177,
                           r.center().y()-21, 115, 44);
    p->setPen(problem ? QColor("#CA7C32") : secondary);
    p->drawText(statusRect.adjusted(0,0,0,-18), Qt::AlignLeft | Qt::AlignVCenter,
                problem ? (m_arabic ? QString::fromUtf8("مشكلة في الجهاز") : QStringLiteral("Needs attention"))
                        : (m_arabic ? QString::fromUtf8("مُثبّت") : QStringLiteral("Installed")));
    p->setPen(secondary);
    const QString version = idx.data(DeviceModel::VersionRole).toString();
    p->drawText(statusRect.adjusted(0,19,0,0), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(font).elidedText(version.isEmpty() ? QStringLiteral("—") : version, Qt::ElideRight, statusRect.width()));
    p->restore();
}

bool DeviceDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                  const QStyleOptionViewItem &option,
                                  const QModelIndex &index)
{
    Q_UNUSED(option);
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Space || key->key() == Qt::Key_Select) {
            const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
            return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
        }
    }
    // The QListView clicked() handler owns mouse toggling: avoid double changes.
    return false;
}
