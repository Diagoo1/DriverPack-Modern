#include "about_page.h"
#include "appearance.h"
#include "vector_icons.h"
#include "theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QPainter>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QTimer>
#include <QElapsedTimer>
#include "ui_language.h"
#include "support_report.h"
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QtMath>

class DeveloperPortrait : public QWidget {
public:explicit DeveloperPortrait(QWidget *p):QWidget(p){setFixedSize(116,116);}
protected:void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE {QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);QRadialGradient glow(QPointF(58,58),58);glow.setColorAt(.7,QColor(42,213,190,75));glow.setColorAt(1,QColor(42,213,190,0));p.setPen(Qt::NoPen);p.setBrush(glow);p.drawEllipse(rect());QPainterPath clip;clip.addEllipse(QRectF(9,9,98,98));p.setClipPath(clip);p.drawPixmap(QRectF(9,9,98,98),QPixmap(QStringLiteral(":/sdio/assets/developer-avatar.jpg")),QRectF(QPixmap(QStringLiteral(":/sdio/assets/developer-avatar.jpg")).rect()));p.setClipping(false);QLinearGradient ring(0,0,116,116);ring.setColorAt(0,QColor("#EAD1A0"));ring.setColorAt(.48,QColor("#59D7C5"));ring.setColorAt(1,QColor("#159389"));p.setPen(QPen(QBrush(ring),2));p.setBrush(Qt::NoBrush);p.drawEllipse(QRectF(7,7,102,102));}
};
class DeveloperCard : public QWidget {
public:explicit DeveloperCard(QWidget *parent):QWidget(parent){setObjectName(QStringLiteral("developerCard"));setMinimumHeight(248);}
protected:void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QRectF area=QRectF(rect()).adjusted(1,1,-1,-1);QPainterPath frame;frame.addRoundedRect(area,23,23);p.setClipPath(frame);
    QLinearGradient base(0,0,width(),height());base.setColorAt(0,QColor("#102B40"));base.setColorAt(.55,QColor("#113E4B"));base.setColorAt(1,QColor("#123A39"));p.fillPath(frame,base);
    const bool rtl=layoutDirection()==Qt::RightToLeft;const QPointF center(rtl?70:width()-70,25);QRadialGradient halo(center,340);halo.setColorAt(0,QColor(51,205,174,48));halo.setColorAt(1,QColor(51,205,174,0));p.fillPath(frame,halo);
    p.setPen(QPen(QColor(142,220,209,23),1));p.setBrush(Qt::NoBrush);for(int i=0;i<3;++i)p.drawEllipse(center,100+i*49,100+i*49);
    QLinearGradient line(0,0,width(),0);line.setColorAt(0,QColor(225,199,148,0));line.setColorAt(.2,QColor(225,199,148,135));line.setColorAt(.7,QColor(61,213,188,110));line.setColorAt(1,QColor(61,213,188,0));p.setPen(QPen(QBrush(line),1));p.drawLine(QPointF(26,1),QPointF(width()-26,1));
    p.setClipping(false);p.setPen(QPen(QColor(109,183,183,65),1));p.drawRoundedRect(area,23,23);
}
};
qreal AboutMotion::heartbeatScale(qint64 elapsedMs){
    const qint64 phase=qMax<qint64>(0,elapsedMs)%1800;
    if(phase<220)return 1+.18*qSin(phase*3.141592653589793/220.);
    if(phase>=310 && phase<500)return 1+.12*qSin((phase-310)*3.141592653589793/190.);
    return 1;
}
class Heartbeat : public QWidget {
public:explicit Heartbeat(QWidget *parent):QWidget(parent){setObjectName(QStringLiteral("signatureHeart"));setFixedSize(32,32);setAccessibleName(UiLanguage::text(QStringLiteral("Love"),QString::fromUtf8("حب")));m_clock.start();m_timer.setSingleShot(true);m_timer.setInterval(33);connect(&m_timer,&QTimer::timeout,this,[this](){if(qApp->property("reduceMotion").toBool()){update();return;}const int phase=int(m_clock.elapsed()%1800);if(phase<550){update();m_timer.start(33);}else m_timer.start(1800-phase);});}
protected:
void showEvent(QShowEvent *e) Q_DECL_OVERRIDE {QWidget::showEvent(e);m_clock.restart();m_timer.start(33);}
void hideEvent(QHideEvent *e) Q_DECL_OVERRIDE {m_timer.stop();QWidget::hideEvent(e);}
void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE {QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.translate(16,17);const qreal scale=qApp->property("reduceMotion").toBool()?1:AboutMotion::heartbeatScale(m_clock.elapsed());p.scale(scale,scale);QPainterPath heart;heart.moveTo(0,8);heart.cubicTo(-5,4,-11,0,-11,-6);heart.cubicTo(-11,-13,-3,-14,0,-7);heart.cubicTo(3,-14,11,-13,11,-6);heart.cubicTo(11,0,5,4,0,8);QLinearGradient rose(-7,-12,7,9);rose.setColorAt(0,QColor("#FFB1BA"));rose.setColorAt(.45,QColor("#F47C96"));rose.setColorAt(1,QColor("#D94F79"));p.setPen(Qt::NoPen);p.setBrush(rose);p.drawPath(heart);}
private:QElapsedTimer m_clock;QTimer m_timer;
};
QWidget *createSignatureHeart(QWidget *parent){return new Heartbeat(parent);}
AboutPage::AboutPage(bool arabic,QWidget *parent):QWidget(parent),m_scroll(new QScrollArea(this)){
    setObjectName(QStringLiteral("page"));QVBoxLayout *root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,20);
    m_scroll->setWidgetResizable(true);m_scroll->setFrameShape(QFrame::NoFrame);m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);root->addWidget(m_scroll);setArabic(arabic);
}
void AboutPage::setArabic(bool ar){
    auto t=[ar](const char *en,const char *arabic){return ar?QString::fromUtf8(arabic):QString::fromUtf8(en);};
    QWidget *previous=m_scroll->takeWidget();if(previous)previous->deleteLater();
    QWidget *body=new QWidget;body->setObjectName(QStringLiteral("aboutBody"));QVBoxLayout *box=new QVBoxLayout(body);box->setContentsMargins(34,27,34,30);box->setSpacing(17);
    auto text=[ar](const QString &value,const char *name,QWidget *parent){QLabel *v=new QLabel(value,parent);v->setObjectName(QString::fromLatin1(name));v->setWordWrap(true);v->setAlignment(Qt::AlignAbsolute|(ar?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter);v->setTextFormat(Qt::PlainText);return v;};
    box->addWidget(text(t("About the project","حول المشروع"),"pageTitle",body));
    box->addWidget(text(t("Thoughtful design. Practical tools. Built for your Windows PC.","تصميم بعناية، وأدوات عملية لجهازك."),"subText",body));
    AccentCard *hero=new AccentCard(body);QHBoxLayout *heroLayout=new QHBoxLayout(hero);heroLayout->setContentsMargins(26,25,26,25);heroLayout->setSpacing(22);
    QLabel *logo=new QLabel(hero);QPixmap mark(QStringLiteral(":/sdio/assets/driverpack-emblem.png"));const qreal dpr=logo->devicePixelRatioF();mark=mark.scaled(qRound(80*dpr),qRound(80*dpr),Qt::KeepAspectRatio,Qt::SmoothTransformation);mark.setDevicePixelRatio(dpr);logo->setPixmap(mark);heroLayout->addWidget(logo);
    QVBoxLayout *heroText=new QVBoxLayout;QLabel *name=text(t("DriverPack Modern","درايفر باك مودرن"),"heroName",hero);name->setStyleSheet(QStringLiteral("color:white;background:transparent;font-size:25px;font-weight:600;"));heroText->addWidget(name);
    QLabel *description=text(t("Find the right driver. Keep the process clear.","اعثر على التعريف المناسب، بخطوات واضحة."),"heroDescription",hero);description->setStyleSheet(QStringLiteral("color:#BDE7E8;background:transparent;font-size:13px;"));heroText->addWidget(description);heroLayout->addLayout(heroText,1);box->addWidget(hero);
    QWidget *developer=new DeveloperCard(body);QHBoxLayout *profile=new QHBoxLayout(developer);profile->setContentsMargins(30,27,30,27);profile->setSpacing(24);profile->addWidget(new DeveloperPortrait(developer),0,Qt::AlignVCenter);
    QVBoxLayout *bio=new QVBoxLayout;QWidget *signature=new QWidget(developer);signature->setObjectName(QStringLiteral("developerSignature"));QHBoxLayout *made=new QHBoxLayout(signature);made->setContentsMargins(11,2,11,2);made->setSpacing(8);made->addWidget(text(t("Made with","صُنع بكل"),"signatureText",signature));made->addWidget(new Heartbeat(signature));made->addWidget(text(t("by","بواسطة"),"signatureText",signature));bio->addWidget(signature,0,Qt::AlignAbsolute|(ar?Qt::AlignRight:Qt::AlignLeft));
    QLabel *author=text(t("Tarek Sadek","طارق صادق"),"developerName",developer);author->setStyleSheet(QStringLiteral("color:#F4FAF7;font-size:32px;font-weight:700;background:transparent;"));bio->addWidget(author);
    bio->addWidget(text(t("Interface design & development · Diagoo1","تصميم وتطوير الواجهة · Diagoo1"),"bodyText",developer));
    bio->addWidget(text(t("Creator of Dreams Software Collection","مطوّر مجموعة دريمز للبرامج"),"subText",developer));profile->addLayout(bio,1);box->addWidget(developer);
    developer->setStyleSheet(QStringLiteral("QLabel {background:transparent;} QWidget#developerSignature {background:rgba(227,241,235,13);border:1px solid rgba(214,206,164,45);border-radius:18px;} QLabel#signatureText {color:#E8DCC2;font-size:15px;font-weight:500;} QLabel#mutedText {color:#C8D8DC;font-size:12px;} QLabel#bodyText {color:#79E0CE;font-size:14px;font-weight:500;} QLabel#subText {color:#BCD1D4;font-size:12px;}"));
    QHBoxLayout *links=new QHBoxLayout;links->setSpacing(12);
    const QStringList titles=QStringList()<<QStringLiteral("GitHub")<<QStringLiteral("PayPal")<<t("Contact","تواصل")<<t("Coffee","قهوة");
    const QStringList urls=QStringList()<<QStringLiteral("https://github.com/Diagoo1")<<QStringLiteral("https://paypal.me/Diagoo1")<<QStringLiteral("mailto:tarek.sadek44@gmail.com")<<QStringLiteral("https://ko-fi.com/diagoo1");
    for(int i=0;i<titles.size();++i){ActionButton *button=new ActionButton(titles[i],body);button->setObjectName(QStringLiteral("outlineButton"));button->setMinimumHeight(44);button->setToolTip(urls[i]);if(i==3){button->setIcon(VectorIcons::icon(QStringLiteral("coffee"),QColor("#009F94"),19));button->setIconSize(QSize(19,19));}const QUrl url(urls[i]);connect(button,&QPushButton::clicked,this,[url](){QDesktopServices::openUrl(url);});button->setIcon(VectorIcons::icon(i==0?QStringLiteral("github"):i==1?QStringLiteral("heart"):i==2?QStringLiteral("mail"):QStringLiteral("coffee"),QColor("#009F94"),19));button->setIconSize(QSize(19,19));button->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);links->addWidget(button,1);}
    ActionButton *report=new ActionButton(t("Bug report","بلاغ"),body);report->setObjectName(QStringLiteral("reportBugButton"));report->setMinimumHeight(44);report->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);report->setIcon(VectorIcons::icon(QStringLiteral("bug"),QColor("#009F94"),22));report->setIconSize(QSize(22,22));report->setToolTip(t("Report a bug","الإبلاغ عن مشكلة"));report->setAccessibleName(report->toolTip());connect(report,&QPushButton::clicked,this,[this](){BugReport::show(this);});links->addWidget(report,1);box->addLayout(links);
    box->addWidget(text(t("Beta · Hardware and Windows compatibility testing is ongoing.","نسخة بيتا · اختبار التوافق مع الأجهزة وإصدارات ويندوز مستمر."),"subText",body));
    QWidget *credits=new SurfaceCard(body);credits->setObjectName(QStringLiteral("softSurface"));QVBoxLayout *creditBox=new QVBoxLayout(credits);creditBox->setContentsMargins(24,18,24,18);creditBox->setSpacing(9);
    creditBox->addWidget(text(t("Built on open source","مبني على برمجيات مفتوحة المصدر"),"sectionTitle",credits));
    creditBox->addWidget(text(t("DriverPack catalog adapter and modern interface: Tarek Sadek. Driver data belongs to its original publishers. Reused interface components retain their original notices. GPL v3 or later.","قارئ فهارس DriverPack والواجهة الحديثة: طارق صادق. بيانات التعريفات لأصحابها، مع حفظ إشعارات حقوق مكونات الواجهة المعاد استخدامها. GPL الإصدار الثالث أو أحدث."),"bodyText",credits));
    creditBox->addWidget(text(t("Application icon designed by Tarek Sadek.","تصميم الأيقونة الرئيسية للبرنامج: طارق صادق."),"bodyText",credits));
    creditBox->addWidget(text(t("Inspired by the developer card in Dreams Software Collection.","مستوحى من بطاقة المطوّر في مجموعة دريمز للبرامج."),"mutedText",credits));box->addWidget(credits);box->addStretch(1);m_scroll->setWidget(body);
}
