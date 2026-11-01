#ifndef SDIO_SEARCHABLE_LANGUAGE_COMBO_H
#define SDIO_SEARCHABLE_LANGUAGE_COMBO_H
#include <QComboBox>
class QFrame;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
// Keeps QComboBox's standard selection model while presenting an app-owned,
// searchable popup; no OS-native combo drop-down chrome is used.
class SearchableLanguageCombo : public QComboBox
{
public:
    explicit SearchableLanguageCombo(QWidget *parent=0);
protected:
    void showPopup() Q_DECL_OVERRIDE;
    void hidePopup() Q_DECL_OVERRIDE;
private:
    void filterItems(const QString &query);
    void choose(QListWidgetItem *item);
    QWidget *m_popup;
    QLineEdit *m_filter;
    QListWidget *m_list;
};
#endif
