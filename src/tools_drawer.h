#ifndef SDIOMODERN_TOOLS_DRAWER_H
#define SDIOMODERN_TOOLS_DRAWER_H

#include <QWidget>
#include <QVector>
#include <QString>

class QToolButton;
class QLabel;
class QPropertyAnimation;

// A native Qt drawer, not a QMenu, HTA, or embedded browser.
// The translucent overlay is below the shared titlebar and blocks the workspace.
class ToolsDrawer : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal openAmount READ openAmount WRITE setOpenAmount)
public:
    explicit ToolsDrawer(QWidget *parent=0);
    qreal openAmount() const { return m_amount; }
    void setOpenAmount(qreal value);
    void setArabic(bool arabic);
    void setMotionEnabled(bool enabled);
    bool isOpened() const { return m_open; }
    void setActionAvailable(const QString &id,bool available,const QString &reason=QString());

public slots:
    void toggle();
    void openDrawer();
    void closeDrawer();

signals:
    void actionRequested(const QString &actionId);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

private:
    struct Row { QToolButton *button; QString english; QString arabic; };
    struct Heading { QLabel *label; QString english; QString arabic; };
    void addHeading(class QVBoxLayout *layout,const QString &english,const QString &arabic);
    void addRow(class QVBoxLayout *layout,const QString &key,const QString &english,
                const QString &arabic,const QString &symbol,bool enabled=true);
    void syncPosition();
    QWidget *m_panel;
    QLabel *m_title;
    QToolButton *m_close;
    QVector<Row> m_rows;
    QVector<Heading> m_headings;
    QPropertyAnimation *m_animation;
    qreal m_amount;
    bool m_open,m_arabic,m_motion;
};

#endif
