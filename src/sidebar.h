#ifndef SDIOMODERN_SIDEBAR_H
#define SDIOMODERN_SIDEBAR_H

#include <QWidget>
#include <QVector>
#include <QAbstractButton>
class QPropertyAnimation;
class QVBoxLayout;
class QLabel;
class QPainter;

class SidebarItem : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal hoverAmount READ hoverAmount WRITE setHoverAmount)
public:
    SidebarItem(const QString &iconName, QWidget *parent=0);
    void setCompact(bool compact);
    void setMotionEnabled(bool enabled);
    qreal hoverAmount() const { return m_hover; }
    void setHoverAmount(qreal value);
protected:
    void nextCheckState() Q_DECL_OVERRIDE;
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void enterEvent(QEvent *event) Q_DECL_OVERRIDE;
    void leaveEvent(QEvent *event) Q_DECL_OVERRIDE;
private:
    QString m_iconName;
    bool m_compact;
    bool m_motion;
    qreal m_hover;
    QPropertyAnimation *m_animation;
};

class Sidebar : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int menuWidth READ menuWidth WRITE setMenuWidth)
    Q_PROPERTY(qreal activeY READ activeY WRITE setActiveY)
    Q_PROPERTY(qreal hoverY READ hoverY WRITE setHoverY)
    Q_PROPERTY(qreal hoverOpacity READ hoverOpacity WRITE setHoverOpacity)
public:
    explicit Sidebar(QWidget *parent=0);
    int menuWidth() const;
    void setMenuWidth(int width);
    void setCurrentPage(int page);
    void setArabic(bool arabic);
    void applyOriginLanguage(const QString &directory,const QString &code);
    void setInstallationAvailable(bool enabled);
    void setAnimations(bool enabled);
    qreal activeY() const {return m_activeY;}
    qreal hoverY() const {return m_hoverY;}
    qreal hoverOpacity() const {return m_hoverOpacity;}
    void setActiveY(qreal value){m_activeY=value;update();}
    void setHoverY(qreal value){m_hoverY=value;update();}
    void setHoverOpacity(qreal value){m_hoverOpacity=value;update();}

signals:
    void pageRequested(int index);

public slots:
    void toggleCollapsed();

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    bool eventFilter(QObject *watched,QEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE;
    void hideEvent(QHideEvent *event) Q_DECL_OVERRIDE;

private:
    SidebarItem *makeButton(int index,const QString &en,const QString &ar,const QString &iconName);
    void refreshLabels();
    void updateActive();
    void syncHighlights();
    void moveHover(SidebarItem *button);
    void animatePosition(QPropertyAnimation *animation,qreal from,qreal to);
    bool motionEnabled() const;
    QVector<SidebarItem*> m_buttons;
    QVector<QString> m_english;
    QVector<QString> m_arabic;
    QPropertyAnimation *m_slide;
    QVBoxLayout *m_layout;
    QLabel *m_footer;
    bool m_collapsed;
    bool m_arabicEnabled;
    bool m_animationsEnabled;
    bool m_labelsHidden;
    int m_current;
    qreal m_activeY,m_hoverY,m_hoverOpacity;
    SidebarItem *m_hoverButton;
    QPropertyAnimation *m_activeMove,*m_hoverMove,*m_hoverFade;
};

#endif
