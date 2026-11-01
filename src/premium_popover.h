#ifndef SDIOMODERN_PREMIUM_POPOVER_H
#define SDIOMODERN_PREMIUM_POPOVER_H

#include <QFrame>
#include <QVector>

class QVBoxLayout;
class QLabel;
class QCheckBox;
class QPushButton;
class QWidget;

// Native Qt popup: no QMenu / operating-system menu frame.
// Uses the app palette in both dark/light themes and closes on outside click.
class PremiumPopover : public QFrame
{
    Q_OBJECT
public:
    explicit PremiumPopover(QWidget *parent=0);
    QLabel *addHeading(const QString &text);
    QCheckBox *addOption(const QString &text,bool checked);
    QPushButton *addLink(const QString &text);
    void addDivider();
    void setCompact();
    void setMotionEnabled(bool enabled) { m_motion=enabled; }
    void setDark(bool dark) { m_dark=dark; update(); }
    void openAt(QWidget *anchor);
    void toggleAt(QWidget *anchor);
protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
private:
    QFrame *m_card;
    QVBoxLayout *m_content;
    bool m_motion,m_dark;
};

#endif
