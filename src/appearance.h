#ifndef SDIOMODERN_APPEARANCE_H
#define SDIOMODERN_APPEARANCE_H
#include <QDialog>
#include <QWidget>
#include <QFrame>
#include <QToolButton>
#include <QPushButton>
#include <QPainterPath>
#include <QMenu>
#include <QProgressBar>
#include <QTimer>
#include <QPropertyAnimation>

class QPainter;

class RoundedActivityBar : public QProgressBar {
    Q_OBJECT
    Q_PROPERTY(qreal visualProgress READ visualProgress WRITE setVisualProgress)
public:explicit RoundedActivityBar(QWidget *parent=0);
    qreal visualProgress()const{return m_visual;}
    void setVisualProgress(qreal value);
    int remainingAnimationMs()const;
protected:void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;void showEvent(QShowEvent*) Q_DECL_OVERRIDE;void hideEvent(QHideEvent*) Q_DECL_OVERRIDE;
private:
    void animateValue();
    QTimer m_tick;int m_phase;
    qreal m_visual;
    QPropertyAnimation m_fill;
};

class TitleButton : public QToolButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverAmount READ hoverAmount WRITE setHoverAmount)
public:
    explicit TitleButton(QWidget *parent=0);
    qreal hoverAmount()const{return m_hover;}
    void setHoverAmount(qreal value){m_hover=value;update();}
protected:
    void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
    void enterEvent(QEvent*) Q_DECL_OVERRIDE;
    void leaveEvent(QEvent*) Q_DECL_OVERRIDE;
    void changeEvent(QEvent*) Q_DECL_OVERRIDE;
private:
    void animateHover(qreal value);
    qreal m_hover;
    QPropertyAnimation m_animation;
};

class RoundedPanel : public QWidget {
public:
    explicit RoundedPanel(QWidget *parent=0);
protected:
    void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
};

class RoundedDialog : public QDialog {
public:
    explicit RoundedDialog(QWidget *parent=0);
protected:
    void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
    void showEvent(QShowEvent*) Q_DECL_OVERRIDE;
};

QPainterPath windowClip(QWidget *widget,qreal radius=20);
void paintPopupShadow(QPainter *painter,const QRectF &face,qreal radius,bool dark);
// Uses the same command surface and sizing as ActionButton in workspace toolbars.
class CommandToolButton : public QToolButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverAmount READ hoverAmount WRITE setHoverAmount)
public:
    explicit CommandToolButton(QWidget *parent=0);
    QSize sizeHint()const Q_DECL_OVERRIDE;
    QSize minimumSizeHint()const Q_DECL_OVERRIDE{return sizeHint();}
    qreal hoverAmount()const{return m_hover;}
    void setHoverAmount(qreal value){m_hover=value;update();}
protected:
    void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
    void enterEvent(QEvent*) Q_DECL_OVERRIDE;
    void leaveEvent(QEvent*) Q_DECL_OVERRIDE;
    void changeEvent(QEvent*) Q_DECL_OVERRIDE;
private:
    void animate(qreal value);
    qreal m_hover;
    QPropertyAnimation m_animation;
};

class ActionButton : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverAmount READ hoverAmount WRITE setHoverAmount)
public:
    explicit ActionButton(const QString &text,QWidget *parent=0);
    QSize sizeHint()const Q_DECL_OVERRIDE;
    QSize minimumSizeHint()const Q_DECL_OVERRIDE{return sizeHint();}
    qreal hoverAmount()const{return m_hover;}
    void setHoverAmount(qreal value){m_hover=value;update();}
protected:
    void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
    void enterEvent(QEvent*) Q_DECL_OVERRIDE;
    void leaveEvent(QEvent*) Q_DECL_OVERRIDE;
private:
    void animate(qreal value);
    qreal m_hover;
    QPropertyAnimation m_animation;
};
class DialogHeader : public QWidget {
public:
    explicit DialogHeader(const QString &title,const QString &symbol,QDialog *dialog,bool arabic=false);
protected:
    void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent*) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent*) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent*) Q_DECL_OVERRIDE;
private:
    QPoint m_offset;bool m_dragging;
};
class AccentCard : public QWidget {
public:
    explicit AccentCard(QWidget *parent=0):QWidget(parent){}
protected:
    void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
};
class SurfaceCard : public QFrame {
public:explicit SurfaceCard(QWidget *parent=0):QFrame(parent){setObjectName(QStringLiteral("softSurface"));}
protected:void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
};
class ProjectMenu : public QMenu {
    Q_OBJECT
    Q_PROPERTY(qreal highlightY READ highlightY WRITE setHighlightY)
public:
    explicit ProjectMenu(QWidget *parent=0);
    qreal highlightY()const{return m_highlightY;}
    void setHighlightY(qreal value){m_highlightY=value;update();}
protected:
    void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
    void showEvent(QShowEvent*) Q_DECL_OVERRIDE;
private:
    qreal m_highlightY;
    QPropertyAnimation m_slide;
};

class HoverSurface : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal amount READ amount WRITE setAmount)
public:
    explicit HoverSurface(QWidget *parent);
    qreal amount()const{return m_amount;}
    void setAmount(qreal value){m_amount=value;update();}
    void transition(bool entered);
protected:
    void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
private:
    qreal m_amount;
    QPropertyAnimation m_animation;
};

void installInterfaceMotion();
bool projectMessage(QWidget *parent,const QString &title,const QString &message,bool question=false,bool arabic=false);
#endif
