#ifndef SDIOMODERN_DEVICE_MODEL_H
#define SDIOMODERN_DEVICE_MODEL_H

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include "device_types.h"

class DeviceModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { ClassRole = Qt::UserRole + 1, ManufacturerRole, VersionRole,
                 ProblemRole, IdRole, HardwareRole, CodeRole, DeviceRole };
    explicit DeviceModel(QObject *parent = 0);
    int rowCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
    bool setData(const QModelIndex &index, const QVariant &value, int role) Q_DECL_OVERRIDE;
    Qt::ItemFlags flags(const QModelIndex &index) const Q_DECL_OVERRIDE;
    void replace(const QVector<DeviceInfo> &devices);
    int problemCount() const;
    int checkedCount() const;
    DeviceInfo deviceAt(int row) const;
    void checkAll(bool value);

signals:
    void checkedCountChanged(int count);

private:
    QVector<DeviceInfo> m_items;
    QVector<bool> m_checked;
};

class DeviceFilter : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit DeviceFilter(QObject *parent = 0);
    void setSearch(const QString &search);
    void setProblemsOnly(bool enabled);
    void setShowInstalled(bool enabled);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const Q_DECL_OVERRIDE;

private:
    QString m_search;
    bool m_problemsOnly;
    bool m_showInstalled;
};

class DeviceDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit DeviceDelegate(QObject *parent = 0);
    void setDark(bool dark);
    void setDetails(bool details);
    void setArabic(bool arabic);
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const Q_DECL_OVERRIDE;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const Q_DECL_OVERRIDE;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) Q_DECL_OVERRIDE;

private:
    bool m_dark;
    bool m_details;
    bool m_arabic;
};

#endif
