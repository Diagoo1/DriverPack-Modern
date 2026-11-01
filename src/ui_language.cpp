#include "ui_language.h"
#include "ui_font.h"
#include <functional>
#include <QVector>
#include <QVariant>
#include <QMenu>
#include <QTabBar>
#include <QSignalBlocker>
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSet>
#include <QLabel>
#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>
#include <QTreeWidget>
#include <QListWidget>
#include <QAbstractItemModel>
#include <QTimer>
#include <QEvent>

namespace UiLanguage {
struct Pattern {QString source,target,prefix;QStringList arguments,literals;};
static QString current=QStringLiteral("english");
static QHash<QString,QString> messages,reverse,previousReverse;
static QVector<Pattern> templates;
static QSet<QString> englishKeys;
static bool updating=false,queued=false;
static QObject *observer=0;
static QJsonObject resource(const QString &name){QFile file(QStringLiteral(":/sdio/i18n/")+name+QStringLiteral(".json"));if(!file.open(QIODevice::ReadOnly) || file.size()>2*1024*1024)return QJsonObject();return QJsonDocument::fromJson(file.readAll()).object();}
static int argumentEnd(const QString &value,int at){if(at<0 || at+1>=value.size() || value.at(at)!=QLatin1Char('%') || value.at(at+1)<QLatin1Char('1') || value.at(at+1)>QLatin1Char('9'))return at;int end=at+2;while(end<value.size() && value.at(end)>=QLatin1Char('0') && value.at(end)<=QLatin1Char('9'))++end;return end;}
static Pattern pattern(const QString &source,const QString &target){
    Pattern result;result.source=source;result.target=target;int position=0;
    for(int at=0;at<source.size();++at){const int end=argumentEnd(source,at);if(end<=at)continue;result.literals.append(source.mid(position,at-position));result.arguments.append(source.mid(at,end-at));position=end;at=end-1;}
    result.literals.append(source.mid(position));result.prefix=result.literals.value(0);return result;
}
static QString substitute(const QString &target,const QHash<QString,QString> &values){
    QString output;int position=0;for(int at=0;at<target.size();++at){const int end=argumentEnd(target,at);if(end<=at)continue;const QString key=target.mid(at,end-at);output+=target.mid(position,at-position)+values.value(key,key);position=end;at=end-1;}return output+target.mid(position);
}
static QString matchTemplate(const QString &value,const QVector<Pattern> &patterns){
    for(const Pattern &p:patterns){
        if(!value.startsWith(p.prefix))continue;int position=p.prefix.size();bool valid=true;QHash<QString,QString> args;
        for(int i=0;i<p.arguments.size();++i){const QString literal=p.literals.value(i+1);const bool last=i==p.arguments.size()-1;const int end=last?value.size()-literal.size():(literal.isEmpty()?-1:value.indexOf(literal,position));
            if(end<position || value.mid(end,literal.size())!=literal){valid=false;break;}const QString argument=value.mid(position,end-position);if(args.contains(p.arguments[i]) && args.value(p.arguments[i])!=argument){valid=false;break;}args.insert(p.arguments[i],argument);position=end+literal.size();}
        if(valid && position==value.size())return substitute(p.target,args);
    }return QString();
}
static QString canonical(const QString &value){
    if(englishKeys.contains(value))return value;if(reverse.contains(value))return reverse.value(value);if(previousReverse.contains(value))return previousReverse.value(value);
    return value;
}
QString text(const QString &english,const QString &arabic){
    if(current==QStringLiteral("arabic") && !arabic.isEmpty())return arabic;
    if(current==QStringLiteral("english") || english.isEmpty() || english.size()>16384)return english;
    if(messages.contains(english))return messages.value(english);
    const QString trimmed=english.trimmed();if(messages.contains(trimmed)){const int first=english.indexOf(trimmed);return english.left(first)+messages.value(trimmed)+english.mid(first+trimmed.size());}
    const QString formatted=matchTemplate(english,templates);if(!formatted.isEmpty())return formatted;
    // Preserve version numbers and device/vendor names while translating UI tags.
    QString result=english;const QStringList separators=QStringList()<<QStringLiteral(" · ")<<QStringLiteral(" — ");for(const QString &separator:separators)if(result.contains(separator)){QStringList parts=result.split(separator);for(QString &part:parts)if(messages.contains(part.trimmed()))part=messages.value(part.trimmed());result=parts.join(separator);}
    return result;
}
QString language(){return current;}
bool isRtl(){return current==QStringLiteral("arabic") || current==QStringLiteral("farsi");}
QList<QPair<QString,QString> > available(){QList<QPair<QString,QString> > result;for(const QJsonValue &entry:resource(QStringLiteral("index")).value(QStringLiteral("locales")).toArray()){const auto item=entry.toObject();result.append(qMakePair(item.value(QStringLiteral("id")).toString(),item.value(QStringLiteral("name")).toString()));}return result;}
static void translateProperty(QWidget *widget,const char *name,const QString &value,const std::function<void(const QString&)> &set){
    const QByteArray sourceName=QByteArray("uiSource_")+name,lastName=QByteArray("uiLast_")+name,localeName=QByteArray("uiLocale_")+name;
    if(widget->property(lastName.constData()).toString()==value && widget->property(localeName.constData()).toString()==current)return;
    const QString source=widget->property(lastName.constData()).toString()==value?widget->property(sourceName.constData()).toString():canonical(value);
    const QString localized=text(source);widget->setProperty(sourceName.constData(),source);widget->setProperty(lastName.constData(),localized);widget->setProperty(localeName.constData(),current);if(localized!=value)set(localized);
}
static void item(QTreeWidgetItem *entry,int columns){
    for(int column=0;column<columns;++column){const QString value=entry->text(column),last=entry->data(column,Qt::UserRole+1001).toString();if(last==value && entry->data(column,Qt::UserRole+1002).toString()==current)continue;
        const QString source=last==value?entry->data(column,Qt::UserRole+1000).toString():canonical(value),localized=text(source);entry->setData(column,Qt::UserRole+1000,source);entry->setData(column,Qt::UserRole+1001,localized);entry->setData(column,Qt::UserRole+1002,current);if(localized!=value)entry->setText(column,localized);}
    for(int i=0;i<entry->childCount();++i)item(entry->child(i),columns);
}
void refresh(){
    queued=false;if(updating || !qApp)return;updating=true;
    const auto widgets=QApplication::allWidgets();
    for(QWidget *widget:widgets){
        if(!widget->isVisible() || !widget->window()->isVisible())continue;
        if(widget->property("skipTranslation").toBool())continue;
        if(widget->property("keepLtr").toBool())widget->setLayoutDirection(Qt::LeftToRight);
        if(QLabel *label=qobject_cast<QLabel*>(widget)){if(!(label->alignment()&Qt::AlignHCenter))label->setAlignment(Qt::AlignAbsolute|(isRtl() && !label->property("keepLtr").toBool()?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter);translateProperty(widget,"text",label->text(),[label](const QString &value){label->setTextFormat(Qt::PlainText);label->setText(value);});}
        else if(QAbstractButton *button=qobject_cast<QAbstractButton*>(widget))translateProperty(widget,"text",button->text(),[button](const QString &value){button->setText(value);});
        else if(QLineEdit *edit=qobject_cast<QLineEdit*>(widget))translateProperty(widget,"placeholder",edit->placeholderText(),[edit](const QString &value){edit->setPlaceholderText(value);});
        if(!widget->toolTip().isEmpty())translateProperty(widget,"tooltip",widget->toolTip(),[widget](const QString &value){widget->setToolTip(value);});
        if(widget->isWindow() && !widget->windowTitle().isEmpty())translateProperty(widget,"title",widget->windowTitle(),[widget](const QString &value){widget->setWindowTitle(value);});
        if(QTabBar *tabs=qobject_cast<QTabBar*>(widget)){QSignalBlocker block(tabs);for(int i=0;i<tabs->count();++i){const QByteArray key=QByteArray("tab")+QByteArray::number(i);translateProperty(tabs,key.constData(),tabs->tabText(i),[tabs,i](const QString &value){tabs->setTabText(i,value);});}}
        if(QMenu *menu=qobject_cast<QMenu*>(widget)){for(QAction *action:menu->actions()){const QString value=action->text(),last=action->property("uiLast").toString();const QString source=last==value?action->property("uiSource").toString():canonical(value),localized=text(source);action->setProperty("uiSource",source);action->setProperty("uiLast",localized);if(localized!=value)action->setText(localized);}}

        if(QTreeWidget *tree=qobject_cast<QTreeWidget*>(widget)){if(tree->objectName()==QStringLiteral("driverTree"))continue;QSignalBlocker block(tree);if(tree->headerItem())item(tree->headerItem(),tree->columnCount());for(int i=0;i<tree->topLevelItemCount();++i)item(tree->topLevelItem(i),tree->columnCount());}
        if(QComboBox *combo=qobject_cast<QComboBox*>(widget)){if(combo->property("languageSelector").toBool())continue;QSignalBlocker block(combo);for(int i=0;i<combo->count();++i){const QString value=combo->itemText(i),last=combo->itemData(i,Qt::UserRole+1001).toString();if(last==value && combo->itemData(i,Qt::UserRole+1002).toString()==current)continue;const QString source=last==value?combo->itemData(i,Qt::UserRole+1000).toString():canonical(value),localized=text(source);combo->setItemData(i,source,Qt::UserRole+1000);combo->setItemData(i,localized,Qt::UserRole+1001);combo->setItemData(i,current,Qt::UserRole+1002);if(localized!=value)combo->setItemText(i,localized);}}

    }
    updating=false;
}
static void schedule(){if(!updating && !queued && observer){queued=true;QTimer::singleShot(25,observer,[]{refresh();});}}
class Observer : public QObject {
public:explicit Observer(QObject *parent):QObject(parent){}
protected:bool eventFilter(QObject *object,QEvent *event) Q_DECL_OVERRIDE {
    if(updating)return false;
    if(event->type()==QEvent::Polish || event->type()==QEvent::Show)schedule();
    return false;
}
};
void install(){if(!observer && qApp){observer=new Observer(qApp);qApp->installEventFilter(observer);}}
void setLanguage(const QString &language){
    install();QString safe=language.toLower();bool valid=!safe.isEmpty();for(QChar c:safe)if((c<QLatin1Char('a') || c>QLatin1Char('z')) && c!=QLatin1Char('_'))valid=false;if(!valid)safe=QStringLiteral("english");
    if(safe==QStringLiteral("hebrew"))safe=QStringLiteral("english");
    previousReverse=reverse;current=safe;messages.clear();reverse.clear();templates.clear();englishKeys.clear();
    const QJsonObject english=resource(QStringLiteral("english")).value(QStringLiteral("messages")).toObject(),localized=resource(safe).value(QStringLiteral("messages")).toObject();
    for(auto it=english.constBegin();it!=english.constEnd();++it)englishKeys.insert(it.key());
    QSet<QString> ambiguous;
    for(auto it=localized.constBegin();it!=localized.constEnd();++it){
        const QString value=it.value().toString();messages.insert(it.key(),value);
        if(reverse.contains(value) && reverse.value(value)!=it.key())ambiguous.insert(value);
        else reverse.insert(value,it.key());
        const Pattern candidate=pattern(it.key(),value);
        // A template must have a literal anchor, so arbitrary device names and
        // numbers cannot accidentally match a general placeholder expression.
        if(!candidate.arguments.isEmpty() && !candidate.prefix.isEmpty())templates.append(candidate);
    }
    for(const QString &value:ambiguous)reverse.remove(value);
    UiFont::applyLanguage(current);
    if(qApp)qApp->setLayoutDirection(isRtl()?Qt::RightToLeft:Qt::LeftToRight);schedule();
}
}
