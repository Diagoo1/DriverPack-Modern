#include "app_update.h"
#include "appearance.h"
#include "ui_language.h"
#include <QCoreApplication>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QCryptographicHash>
#include <QThread>
#include <QTimer>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include "theme.h"
#include <QPushButton>
#include <QProcess>
#include <QUrl>
#include <QtEndian>
#include <atomic>
#include <functional>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wininet.h>
#endif
namespace AppUpdate {
QString projectRepository(){return QStringLiteral("Diagoo1/DriverPack-Modern");}
static void fail(const char *message){throw std::runtime_error(message);}
bool validRepository(const QString &value){return QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$")).match(value).hasMatch() && !value.contains(QStringLiteral(".."));}
static QList<int> version(QString input){if(input.startsWith(QLatin1Char('v')))input.remove(0,1);if(!QRegularExpression(QStringLiteral("^[0-9]{1,5}\\.[0-9]{1,5}\\.[0-9]{1,5}$")).match(input).hasMatch())fail("Unsupported release version.");QList<int> result;for(const QString &part:input.split(QLatin1Char('.')))result.append(part.toInt());return result;}
bool newer(const QString &candidate,const QString &installed){const auto a=version(candidate),b=version(installed);for(int i=0;i<3;++i){if(a[i]!=b[i])return a[i]>b[i];}return false;}
Release parseRelease(const QJsonObject &data,const QString &repository){
    if(!validRepository(repository) || data.value(QStringLiteral("draft")).toBool(true) || data.value(QStringLiteral("prerelease")).toBool(true))fail("Only published stable releases are accepted.");
    Release result;result.version=data.value(QStringLiteral("tag_name")).toString();version(result.version);
    const QString prefix=QStringLiteral("https://github.com/")+repository+QStringLiteral("/releases/");result.page=data.value(QStringLiteral("html_url")).toString();if(!result.page.startsWith(prefix))fail("The release does not belong to the configured repository.");
    for(const auto &value:data.value(QStringLiteral("assets")).toArray()){
        const auto asset=value.toObject();if(asset.value(QStringLiteral("name")).toString()!=QStringLiteral("DriverPackModern.exe"))continue;
        if(!result.url.isEmpty())fail("Duplicate release executables.");result.url=asset.value(QStringLiteral("browser_download_url")).toString();result.size=qint64(asset.value(QStringLiteral("size")).toDouble());
        const QString digest=asset.value(QStringLiteral("digest")).toString();if(!QRegularExpression(QStringLiteral("^sha256:[0-9a-f]{64}$")).match(digest).hasMatch())fail("GitHub did not provide the required SHA-256 digest.");result.sha256=digest.mid(7).toLatin1();
        const QUrl url(result.url);if(!result.url.startsWith(prefix+QStringLiteral("download/")) || !url.userInfo().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty() || result.size<1024 || result.size>128*1024*1024)fail("Invalid release asset.");
    }
    if(result.url.isEmpty())fail("The release has no portable application executable.");return result;
}
static bool executable(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()<1024)return false;const QByteArray head=file.read(64);if(!head.startsWith("MZ"))return false;const quint32 offset=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(head.constData()+60));if(offset>quint64(file.size()-24) || !file.seek(offset))return false;const auto pe=file.read(24);return pe.size()==24 && pe.startsWith(QByteArray("PE\0\0",4)) && qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(pe.constData()+4))==0x14c;}
static QByteArray hashFile(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))fail("Cannot read the update executable.");QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))fail("Cannot verify the update executable.");return hash.result().toHex();}
void replaceVerified(const QString &target,const QString &source,const QByteArray &digest){
    if(QFileInfo(target).isSymLink() || QFileInfo(source).isSymLink() || !executable(target) || !executable(source) || QFileInfo(source).size()>128*1024*1024 || hashFile(source)!=digest)fail("Update executable validation failed.");
    QFile input(source);QSaveFile output(target);if(!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))fail("Cannot replace the application executable.");QCryptographicHash written(QCryptographicHash::Sha256);
    while(!input.atEnd()){const QByteArray bytes=input.read(65536);if(bytes.isEmpty() || output.write(bytes)!=bytes.size())fail("Cannot write the application update.");written.addData(bytes);}
    if(written.result().toHex()!=digest)fail("The staged update changed during replacement.");input.close();if(!output.commit())fail("Cannot commit the application update. The existing executable was preserved.");
}
#ifdef Q_OS_WIN
struct Internet {HINTERNET handle;explicit Internet(HINTERNET value):handle(value){}~Internet(){if(handle)InternetCloseHandle(handle);}};
static QByteArray fetch(QUrl url,const std::atomic<bool> &stop,QSaveFile *file=0,qint64 expected=0,const QByteArray &digest=QByteArray()){
    Internet session(InternetOpenW(L"DriverPack Modern / project updates",INTERNET_OPEN_TYPE_PRECONFIG,0,0,0));if(!session.handle)fail("Cannot open the Windows Internet session.");DWORD timeout=15000;for(DWORD option:{INTERNET_OPTION_CONNECT_TIMEOUT,INTERNET_OPTION_RECEIVE_TIMEOUT,INTERNET_OPTION_SEND_TIMEOUT})InternetSetOptionW(session.handle,option,&timeout,sizeof(timeout));
    for(int redirects=0;redirects<6;++redirects){
        if(stop.load())fail("Update cancelled.");const QString host=url.host().toLower();if(url.scheme()!=QStringLiteral("https") || !url.userInfo().isEmpty() || (host!=QStringLiteral("api.github.com") && host!=QStringLiteral("github.com") && host!=QStringLiteral("release-assets.githubusercontent.com") && host!=QStringLiteral("objects.githubusercontent.com")))fail("The update redirected outside GitHub HTTPS hosts.");
        const QString encoded=QString::fromLatin1(url.toEncoded());const wchar_t *headers=L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\nAccept-Encoding: identity\r\n";
        Internet request(InternetOpenUrlW(session.handle,reinterpret_cast<LPCWSTR>(encoded.utf16()),headers,DWORD(-1),INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_AUTH|INTERNET_FLAG_NO_UI|INTERNET_FLAG_NO_AUTO_REDIRECT,0));if(!request.handle)fail("Cannot connect to GitHub. Check Windows TLS support and the Internet connection.");
        DWORD status=0,length=sizeof(status);if(!HttpQueryInfoW(request.handle,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&status,&length,0))fail("No HTTP status received.");
        if(status==301 || status==302 || status==303 || status==307 || status==308){wchar_t target[8192];length=sizeof(target);if(!HttpQueryInfoW(request.handle,HTTP_QUERY_LOCATION,target,&length,0))fail("Missing update redirect.");url=url.resolved(QUrl(QString::fromWCharArray(target)));continue;}
        if(status==404 && url.path().endsWith(QStringLiteral("/releases/latest")))fail("No stable release is published yet. Open Browse releases in Settings to download the current Beta.");
        if(status!=200)fail("GitHub could not provide this release. Check the repository and published releases.");
        QByteArray output;QCryptographicHash hash(QCryptographicHash::Sha256);qint64 total=0;char buffer[65536];DWORD read=0;
        for(;;){if(stop.load())fail("Update cancelled.");if(!InternetReadFile(request.handle,buffer,sizeof(buffer),&read))fail("The application update transfer failed.");if(!read)break;total+=read;if(total>(file?expected:2*1024*1024))fail("The update response exceeded its expected size.");if(file){if(file->write(buffer,read)!=read)fail("Cannot save the application update.");hash.addData(buffer,read);}else output.append(buffer,read);}
        if(file && (total!=expected || hash.result().toHex()!=digest))fail("Application update integrity verification failed.");return output;
    }fail("Too many update redirects.");return QByteArray();
}
#else
static QByteArray fetch(QUrl,const std::atomic<bool>&,QSaveFile * =0,qint64=0,const QByteArray & =QByteArray()){fail("Application updates require Windows.");return QByteArray();}
#endif
Release latest(const QString &repository,const std::atomic<bool> &stop){
    if(!validRepository(repository))fail("Invalid project repository.");
    return parseRelease(QJsonDocument::fromJson(fetch(QUrl(QStringLiteral("https://api.github.com/repos/")+repository+QStringLiteral("/releases/latest")),stop)).object(),repository);
}
void downloadVerified(const Release &release,const QString &path,const std::atomic<bool> &stop){
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))fail("Cannot create the staged update file.");
    fetch(QUrl(release.url),stop,&file,release.size,release.sha256);
    if(!file.commit() || !executable(path))fail("The verified release is not a supported portable executable.");
}
class Worker : public QThread {
public:std::atomic<bool> stop;QString error;std::function<void()> operation;Worker():stop(false){}~Worker(){stop.store(true);wait();}
protected:void run() Q_DECL_OVERRIDE {try{operation();}catch(const std::exception &e){error=QString::fromUtf8(e.what());}}
};
static bool wait(QWidget *parent,Worker &worker,const QString &caption){
    RoundedDialog dialog(parent);dialog.setStyleSheet(Theme::styleSheet(parent && parent->window()->property("darkMode").toBool()));dialog.setFixedWidth(510);QVBoxLayout *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);layout->addWidget(new DialogHeader(UiLanguage::text(QStringLiteral("Application update")),QStringLiteral("refresh"),&dialog,UiLanguage::isRtl()));QWidget *content=new QWidget(&dialog);QVBoxLayout *body=new QVBoxLayout(content);body->setContentsMargins(26,24,26,24);body->setSpacing(18);QLabel *label=new QLabel(caption,content);label->setObjectName(QStringLiteral("bodyText"));label->setAlignment(Qt::AlignAbsolute|(UiLanguage::isRtl()?Qt::AlignRight:Qt::AlignLeft));label->setWordWrap(true);body->addWidget(label);QProgressBar *progress=new RoundedActivityBar(content);progress->setObjectName(QStringLiteral("mainProgress"));progress->setRange(0,0);progress->setFixedHeight(10);progress->setTextVisible(false);body->addWidget(progress);QHBoxLayout *actions=new QHBoxLayout;actions->addStretch();ActionButton *cancel=new ActionButton(UiLanguage::text(QStringLiteral("Cancel")),content);cancel->setObjectName(QStringLiteral("outlineButton"));actions->addWidget(cancel);body->addLayout(actions);layout->addWidget(content);QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);QObject::connect(&worker,&QThread::finished,&dialog,&QDialog::accept);worker.start();const bool accepted=dialog.exec()==QDialog::Accepted;if(!accepted)worker.stop.store(true);worker.wait();if(!accepted)return false;if(!worker.error.isEmpty()){projectMessage(parent,UiLanguage::text(QStringLiteral("Application update")),worker.error);return false;}return true;
}
void check(QWidget *parent,const QString &repository){
    Q_UNUSED(repository);projectMessage(parent,UiLanguage::text(QStringLiteral("Application updates"),QString::fromUtf8("تحديث البرنامج")),UiLanguage::text(QStringLiteral("DriverPack Modern has no published release channel yet."),QString::fromUtf8("لم يتم تجهيز قناة إصدارات لدرايفر باك مودرن بعد.")));
}
static int applyFiles(const QStringList &arguments){
    if(arguments.size()!=6 || arguments[1]!=QStringLiteral("--apply-update"))return 2;
    const QString target=arguments[2],source=arguments[3],digest=arguments[4],directory=QCoreApplication::applicationDirPath();bool ok=false;const quint32 pid=arguments[5].toUInt(&ok);
    if(!ok || !pid || QFileInfo(target).fileName()!=QStringLiteral("DriverPackModern.exe") || QFileInfo(source).absolutePath()!=directory || QFileInfo(source).fileName()!=QStringLiteral("verified-update.exe") || !QFileInfo(directory).fileName().startsWith(QStringLiteral("sdi-update-")) || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(digest).hasMatch())return 2;
    try{if(!executable(target) || !executable(source) || hashFile(source)!=digest.toLatin1())return 3;
#ifdef Q_OS_WIN
        HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!parent)return 4;const DWORD ended=WaitForSingleObject(parent,60000);CloseHandle(parent);if(ended!=WAIT_OBJECT_0)return 4;
#endif
        replaceVerified(target,source,digest.toLatin1());
        if(hashFile(target)!=digest.toLatin1())return 6;QFile::remove(source);return QProcess::startDetached(target,QStringList()<<QStringLiteral("--no-splash"))?0:7;
    }catch(const std::exception&){return 3;}
}
int apply(int argc,char **argv){QApplication app(argc,argv);const int result=applyFiles(app.arguments());if(result)projectMessage(0,QStringLiteral("Application update"),QStringLiteral("The update could not be applied (code %1). The existing application was preserved. Check folder permissions and open the application again.").arg(result));return result;}
}
