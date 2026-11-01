#ifndef SDIOMODERN_TITLE_BAR_H
#define SDIOMODERN_TITLE_BAR_H

#include <QWidget>
#include <QPoint>
class QToolButton;
class QLabel;
class QMouseEvent;

// An actual native Qt Widgets titlebar: no HTML engine and no .NET dependency.
// Window actions work for a frameless top-level window on XP to Windows 11.
class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = 0);
    void setMaximized(bool maximized);
    void setStatus(const QString &status);
    void setRefreshEnabled(bool enabled);
    void setArabic(bool arabic);
    void setDark(bool dark);
    void setWorkflowLocked(bool locked);
    bool isDragArea(const QPoint &position) const;

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();
    void menuRequested();
    void sidebarRequested();
    void refreshRequested();
    void themeRequested(bool dark);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseDoubleClickEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

private:
    void updateTooltips();
    QToolButton *createButton(const QString &symbol, const QString &objectName,
                              const QString &description);
    QToolButton *m_menu;
    QToolButton *m_collapse;
    QToolButton *m_refresh;
    QToolButton *m_theme;
    QToolButton *m_min;
    QToolButton *m_max;
    QToolButton *m_close;
    QLabel *m_status;
    QPoint m_dragOffset;
    bool m_dragging;
    bool m_arabic;
    bool m_maximized;
    bool m_workflowLocked;
    bool m_dark=false;
};

#endif
