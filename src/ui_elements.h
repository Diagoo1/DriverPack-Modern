#ifndef SDIOMODERN_UI_ELEMENTS_H
#define SDIOMODERN_UI_ELEMENTS_H

#include <QPushButton>
#include <QAbstractButton>
#include <QWidget>
#include <QColor>
#include <QIcon>
#include <QPixmap>

QIcon makeNavIcon(int type, const QColor &color);
QIcon makeToolbarIcon(int type, const QColor &color);
QPixmap makeBrandMark(int diameter, bool light);

class QPropertyAnimation;

class GradientButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(qreal hoverLevel READ hoverLevel WRITE setHoverLevel)
    Q_PROPERTY(qreal textBlend READ textBlend WRITE setTextBlend)
public:
    explicit GradientButton(const QString &text, QWidget *parent = 0);
    qreal hoverLevel() const { return m_hover; }
    void setHoverLevel(qreal x);
    qreal textBlend() const { return m_textBlend; }
    void setTextBlend(qreal value);
    void setActionState(const QString &text,const QString &iconName);
    void setMotionEnabled(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void enterEvent(QEvent *event) Q_DECL_OVERRIDE;
    void leaveEvent(QEvent *event) Q_DECL_OVERRIDE;

private:
    qreal m_hover;
    QPropertyAnimation *m_hoverAnim;
    QPropertyAnimation *m_textAnim;
    qreal m_textBlend;
    QString m_oldText,m_oldIcon,m_iconName;
    bool m_motionEnabled;
};

class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal position READ position WRITE setPosition)
public:
    explicit ToggleSwitch(QWidget *parent = 0);
    QSize sizeHint() const Q_DECL_OVERRIDE;
    qreal position() const { return m_position; }
    void setPosition(qreal x);
    void setMotionEnabled(bool enabled);
    void setDark(bool dark);
protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
private:
    qreal m_position;
    bool m_animations;
    bool m_dark;
    QPropertyAnimation *m_anim;
};

class Splash : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int progress READ progress WRITE setProgress)
public:
    explicit Splash(QWidget *parent = 0);
    int progress() const { return m_progress; }
    void setProgress(int progress);
    void begin();

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;

private:
    int m_progress;
};

#endif
