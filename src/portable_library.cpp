#include "portable_library.h"
#include "app_settings.h"
#include <QScopedPointer>
#include "../native/driverpack_catalog.h"
#include "appearance.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QStorageInfo>
#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QCheckBox>
#include <QTemporaryDir>
#include <QFileDialog>
#include <stdexcept>

class PortableWorker : public QThread {
public:explicit PortableWorker(QObject *parent):QThread(parent),stop(false),done(0),total(0){}
    QVector<PortableLibrary::Entry> files;QString destination,error;std::atomic<bool> stop;std::atomic<qint64> done,total;
protected:void run() Q_DECL_OVERRIDE {try{PortableLibrary::copy(files,destination,stop,[this](qint64 n,qint64 all){done.store(n);total.store(all);});}catch(const std::exception &e){error=QString::fromUtf8(e.what());}}
};
void openPortableLibrary(QWidget *parent,const QString &drivers,const QString &indexes,const QStringList &needed,bool ar){
    auto t=[ar](const char *en,const char *arabic){return ar?QString::fromUtf8(arabic):QString::fromUtf8(en);};
    RoundedDialog dialog(parent);dialog.setMinimumWidth(610);dialog.setWindowTitle(t("Portable library","مكتبة محمولة"));QVBoxLayout *frame=new QVBoxLayout(&dialog);frame->setContentsMargins(0,0,0,0);frame->setSpacing(0);frame->addWidget(new DialogHeader(t("Create a portable library","إنشاء مكتبة محمولة"),QStringLiteral("usb"),&dialog,ar));
    QWidget *body=new QWidget(&dialog);QVBoxLayout *box=new QVBoxLayout(body);box->setContentsMargins(25,22,25,25);box->setSpacing(15);frame->addWidget(body);
    QLabel *intro=new QLabel(t("Copy the program, packages and indexes to a USB drive or another folder. Unrelated files are kept. No drive is formatted.","انسخ البرنامج والحزم والفهارس إلى USB أو مجلد آخر. الملفات الأخرى تبقى كما هي، دون تهيئة أي قرص."),body);intro->setWordWrap(true);intro->setObjectName(QStringLiteral("bodyText"));box->addWidget(intro);
    QCheckBox *onlyNeeded=new QCheckBox(t("Only packages recommended for this computer","الحزم الموصى بها لهذا الجهاز فقط"),body);onlyNeeded->setEnabled(!needed.isEmpty());onlyNeeded->setChecked(!needed.isEmpty());box->addWidget(onlyNeeded);
    QCheckBox *includeSoftware=new QCheckBox(t("Include ordinary local software (no partner packages)","تضمين البرامج المحلية العادية بدون حزم الشراكات"),body);{QScopedPointer<QSettings> preferences(createAppSettings(QString(),0,false));includeSoftware->setChecked(preferences->value(QStringLiteral("offline/category0"),true).toBool());}box->addWidget(includeSoftware);QTemporaryDir softwareData;
    ActionButton *choose=new ActionButton(t("Choose destination & copy","اختيار الوجهة والنسخ"),body);choose->setObjectName(QStringLiteral("primaryButton"));box->addWidget(choose);
    QProgressBar *bar=new RoundedActivityBar(body);bar->setTextVisible(true);bar->setFixedHeight(24);bar->setObjectName(QStringLiteral("mainProgress"));bar->setRange(0,1000);bar->setValue(0);box->addWidget(bar);
    QLabel *status=new QLabel(t("One executable. Your offline library.","ملف تشغيلي واحد، ومكتبتك الأوفلاين."),body);status->setObjectName(QStringLiteral("subText"));status->setWordWrap(true);box->addWidget(status);
    PortableWorker worker(&dialog);QTimer timer;timer.setInterval(100);QObject::connect(&timer,&QTimer::timeout,&dialog,[&](){const qint64 all=worker.total.load();if(all)bar->setValue(static_cast<int>(worker.done.load()*1000/all));});
    QObject::connect(choose,&QPushButton::clicked,&dialog,[&](){
        const QString folder=QFileDialog::getExistingDirectory(&dialog,t("Choose USB drive or folder","اختيار USB أو مجلد"));if(folder.isEmpty())return;
        worker.files.clear();worker.destination=QDir(folder).filePath(QStringLiteral("DriverPack Modern"));worker.stop.store(false);worker.error.clear();worker.done.store(0);worker.total.store(0);
        try{worker.files=PortableLibrary::entries(QCoreApplication::applicationFilePath(),drivers,indexes,needed,onlyNeeded->isChecked());
         if(includeSoftware->isChecked()){if(!softwareData.isValid())throw std::runtime_error("Cannot stage the portable software catalog.");worker.files+=PortableLibrary::softwareEntries(DriverPackSoftware::load(QFileInfo(drivers).absolutePath(),true),QDir(softwareData.path()).filePath(QStringLiteral("catalog.json")));}}
        catch(const std::exception &e){status->setText(QString::fromUtf8(e.what()));return;}
        choose->setEnabled(false);onlyNeeded->setEnabled(false);includeSoftware->setEnabled(false);status->setText(t("Copying and verifying files...","جارٍ نسخ الملفات والتحقق منها..."));timer.start();worker.start();
    });
    QObject::connect(&worker,&QThread::finished,&dialog,[&](){timer.stop();if(worker.error.isEmpty()){bar->setRange(0,1000);bar->setValue(1000);}choose->setEnabled(true);includeSoftware->setEnabled(true);onlyNeeded->setEnabled(!needed.isEmpty());status->setText(worker.error.isEmpty()?t("Portable library ready: ","المكتبة المحمولة جاهزة: ")+worker.destination:worker.error);});
    dialog.exec();worker.stop.store(true);worker.wait();
}
