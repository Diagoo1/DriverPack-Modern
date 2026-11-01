#include "system_profile.h"
#include "pnp_probe.h"
#include "ui_language.h"
#include <QRegularExpression>
#include <QApplication>
#include <QDesktopWidget>
#include <QSettings>
#include <QSysInfo>
#include <QStringList>
#include <QSet>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#endif

static QString t(const char *english,const char *arabic){return UiLanguage::text(QString::fromUtf8(english),QString::fromUtf8(arabic));}
QString SystemProfile::displayValue(const QString &key,const QString &raw)
{
    // Only known UI values and field formats are translated. Device, model,
    // vendor and identifier text is opaque, including names with punctuation.
    if(raw==QStringLiteral("Not reported"))return t("Not reported","غير متاح");
    if(raw==QStringLiteral("Enabled"))return t("Enabled","مفعّل");
    if(raw==QStringLiteral("Disabled"))return t("Disabled","معطّل");
    if(raw==QStringLiteral("AC power"))return t("AC power","متصل بالكهرباء");
    if(raw==QStringLiteral("Battery / offline"))return t("Battery / offline","يعمل على البطارية");
    if(raw==QStringLiteral("No system battery reported"))return t("No system battery reported","لا توجد بطارية مسجّلة للنظام");
    if(raw==QStringLiteral("Demo inventory — not measured"))return t("Demo inventory — not measured","بيانات معاينة وليست قياسًا فعليًا");
    if(raw==QStringLiteral("Not detected from matched devices"))return t("Not detected from matched devices","لم يُكتشف ضمن الأجهزة المطابقة");
    if(raw==QStringLiteral("Not detected among present PnP devices"))return t("Not detected among present PnP devices","لم يُكتشف ضمن الأجهزة المتصلة");
    if(raw==QStringLiteral("Not reported by display driver"))return t("Not reported by display driver","تعريف الشاشة لا يوفّر هذه المعلومة");
    if(key==QStringLiteral("architecture")){
        if(raw==QStringLiteral("x64 (64-bit)"))return t("x64 (64-bit)","64 بت (x64)");
        if(raw==QStringLiteral("x86 (32-bit)"))return t("x86 (32-bit)","32 بت (x86)");
    }
    auto match=[&raw](const char *pattern){return QRegularExpression(QString::fromLatin1(pattern)).match(raw);};
    QRegularExpressionMatch m;
    if(key==QStringLiteral("logical") && (m=match("^([0-9]+) logical processor\\(s\\)$")).hasMatch())return t("%1 logical processor(s)","%1 معالج منطقي").arg(m.captured(1));
    if(key==QStringLiteral("clock") && (m=match("^([0-9]+) MHz \\(reported\\)$")).hasMatch())return t("%1 MHz (reported)","%1 ميغاهرتز (مسجّل)").arg(m.captured(1));
    if(key==QStringLiteral("uptime") && (m=match("^([0-9]+) d  ([0-9]+) h  ([0-9]+) min$")).hasMatch())return t("%1 d  %2 h  %3 min","%1 يوم · %2 ساعة · %3 دقيقة").arg(m.captured(1),m.captured(2),m.captured(3));
    if(key==QStringLiteral("memoryload") && (m=match("^([0-9]+)% in use$")).hasMatch())return t("%1% in use","%1% قيد الاستخدام").arg(m.captured(1));
    if(key==QStringLiteral("volume") && (m=match("^(.+)  ·  (.+) total  ·  (.+) free$")).hasMatch())return t("%1  ·  %2 total  ·  %3 free","%1 · الإجمالي %2 · المتاح %3").arg(m.captured(1),m.captured(2),m.captured(3));
    if(key==QStringLiteral("resolution") && (m=match("^([0-9]+) × ([0-9]+)  ·  ([0-9]+) screen\\(s\\)$")).hasMatch())return t("%1 × %2  ·  %3 screen(s)","%1 × %2 · عدد الشاشات: %3").arg(m.captured(1),m.captured(2),m.captured(3));
    if(key==QStringLiteral("refresh") && (m=match("^([0-9]+) Hz \\(primary display\\)$")).hasMatch())return t("%1 Hz (primary display)","%1 هرتز (الشاشة الرئيسية)").arg(m.captured(1));
    if(key==QStringLiteral("issues") && (m=match("^([0-9]+) present PnP devices  ·  ([0-9]+) matched  ·  ([0-9]+) warnings in matches$")).hasMatch())return t("%1 present PnP devices  ·  %2 matched  ·  %3 warnings in matches","%1 جهاز متصل · %2 مطابق · %3 تحذيرات في المطابقات").arg(m.captured(1),m.captured(2),m.captured(3));
    if(key==QStringLiteral("issues") && (m=match("^([0-9]+) matched devices  ·  ([0-9]+) reported warnings$")).hasMatch())return t("%1 matched devices  ·  %2 reported warnings","%1 جهاز مطابق · %2 تحذيرات مسجّلة").arg(m.captured(1),m.captured(2));
    if(key==QStringLiteral("power") && (m=match("^(.+)  ·  ([0-9]+)% battery$")).hasMatch())return t("%1  ·  %2% battery","%1 · البطارية %2%").arg(displayValue(key,m.captured(1)),m.captured(2));
    const QStringList groups=QStringList()<<QStringLiteral("graphics")<<QStringLiteral("storage")<<QStringLiteral("network")<<QStringLiteral("wireless")<<QStringLiteral("audio")<<QStringLiteral("bluetooth")<<QStringLiteral("usb")<<QStringLiteral("battery")<<QStringLiteral("tpm");
    if(groups.contains(key) && (m=match("^(.+) \\(\\+([0-9]+) more\\)$")).hasMatch())return t("%1 (+%2 more)","%1 (و%2 أخرى)").arg(m.captured(1),m.captured(2));
    return raw;
}

static QString findDevice(const QVector<DeviceInfo> &items,const QStringList &classes,
                          const QStringList &nameParts=QStringList(),int maxCount=2)
{
    QSet<QString> seen;
    QStringList found;
    for(int i=0;i<items.size();++i){
        const DeviceInfo &d=items.at(i);
        bool match=false;
        for(int j=0;j<classes.size();++j)
            if(d.deviceClass.contains(classes.at(j),Qt::CaseInsensitive)){match=true;break;}
        if(!match){
            for(int j=0;j<nameParts.size();++j)
                if(d.name.contains(nameParts.at(j),Qt::CaseInsensitive)){match=true;break;}
        }
        if(match && !d.name.trimmed().isEmpty() && !seen.contains(d.name)){
            seen.insert(d.name);found.append(d.name);
            if(found.size()>=maxCount)break;
        }
    }
    return found.isEmpty()?QStringLiteral("Not detected from matched devices"):found.join(QStringLiteral(" · "));
}

#ifdef Q_OS_WIN
static QString registryValue(const QString &root,const QString &name)
{
    QSettings reg(root,QSettings::NativeFormat);
    const QString value=reg.value(name).toString().trimmed();
    return value.isEmpty()?QStringLiteral("Not reported"):value;
}
static QString formatGiB(quint64 n)
{
    return QStringLiteral("%1 GiB").arg(static_cast<double>(n)/(1024.0*1024.0*1024.0),0,'f',1);
}
static QString formatUptime()
{
    quint64 ms=0;
    HMODULE kernel=GetModuleHandleW(L"kernel32.dll");
    typedef ULONGLONG (WINAPI *Tick64)();
    const Tick64 fn=kernel?reinterpret_cast<Tick64>(GetProcAddress(kernel,"GetTickCount64")):0;
    ms=fn?static_cast<quint64>(fn()):static_cast<quint64>(GetTickCount());
    const quint64 minutes=ms/60000;
    return QStringLiteral("%1 d  %2 h  %3 min").arg(minutes/1440).arg((minutes/60)%24).arg(minutes%60);
}
#endif

QMap<QString,QString> SystemProfile::collect(const QVector<DeviceInfo> &devices,bool demo)
{
    QMap<QString,QString> facts;
    const QString missing=QStringLiteral("Not reported");
    facts.insert(QStringLiteral("os"),QSysInfo::prettyProductName());
    facts.insert(QStringLiteral("architecture"),QSysInfo::currentCpuArchitecture());
    facts.insert(QStringLiteral("computer"),QString::fromLocal8Bit(qgetenv("COMPUTERNAME")));
    if(facts.value(QStringLiteral("computer")).isEmpty())facts[QStringLiteral("computer")]=missing;
#ifdef Q_OS_WIN
    if(facts.value(QStringLiteral("computer"))==missing){
        WCHAR buf[MAX_COMPUTERNAME_LENGTH+1]={0};DWORD size=MAX_COMPUTERNAME_LENGTH+1;
        if(GetComputerNameW(buf,&size))facts[QStringLiteral("computer")]=QString::fromWCharArray(buf);
    }
#endif

#ifdef Q_OS_WIN
    const QString bios=QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS");
    const QString cpu=QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
    const QString osReg=QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
    const QString product=registryValue(osReg,QStringLiteral("ProductName"));
    const QString build=registryValue(osReg,QStringLiteral("CurrentBuildNumber"));
    bool buildOk=false;
    const int buildNumber=build.toInt(&buildOk);
    if(product!=missing){
        QString osName=product;
        if(buildOk && buildNumber>=22000 && product.contains(QStringLiteral("Windows 10")))
            osName.replace(QStringLiteral("Windows 10"),QStringLiteral("Windows 11"));
        facts[QStringLiteral("os")]=osName;
    }
    const QString displayVersion=registryValue(osReg,QStringLiteral("DisplayVersion"));
    const QString revision=registryValue(osReg,QStringLiteral("UBR"));
    facts[QStringLiteral("build")]=build==missing?missing:
       (build+(revision==missing?QString():QStringLiteral(".")+revision)+
       (displayVersion==missing?QString():QStringLiteral("  ·  ")+displayVersion));

    SYSTEM_INFO sys;
    ZeroMemory(&sys,sizeof(sys));
    GetNativeSystemInfo(&sys);
    switch(sys.wProcessorArchitecture){
        case 9:facts[QStringLiteral("architecture")]=QStringLiteral("x64 (64-bit)");break;
        case 0:facts[QStringLiteral("architecture")]=QStringLiteral("x86 (32-bit)");break;
        case 12:facts[QStringLiteral("architecture")]=QStringLiteral("ARM64");break;
        default:break;
    }
    facts.insert(QStringLiteral("logical"),QStringLiteral("%1 logical processor(s)").arg(sys.dwNumberOfProcessors));
    facts.insert(QStringLiteral("cpu"),registryValue(cpu,QStringLiteral("ProcessorNameString")));
    const QString mhz=registryValue(cpu,QStringLiteral("~MHz"));
    facts.insert(QStringLiteral("clock"),mhz==missing?missing:mhz+QStringLiteral(" MHz (reported)"));
    facts.insert(QStringLiteral("uptime"),formatUptime());

    facts.insert(QStringLiteral("board"),registryValue(bios,QStringLiteral("BaseBoardProduct")));
    facts.insert(QStringLiteral("boardmaker"),registryValue(bios,QStringLiteral("BaseBoardManufacturer")));
    facts.insert(QStringLiteral("boardversion"),registryValue(bios,QStringLiteral("BaseBoardVersion")));
    facts.insert(QStringLiteral("bios"),registryValue(bios,QStringLiteral("BIOSVersion")));
    facts.insert(QStringLiteral("biosvendor"),registryValue(bios,QStringLiteral("BIOSVendor")));
    facts.insert(QStringLiteral("biosdate"),registryValue(bios,QStringLiteral("BIOSReleaseDate")));
    const QString manufacturer=registryValue(bios,QStringLiteral("SystemManufacturer"));
    const QString model=registryValue(bios,QStringLiteral("SystemProductName"));
    facts.insert(QStringLiteral("maker"),manufacturer);
    facts.insert(QStringLiteral("model"),model);
    MEMORYSTATUSEX mem;
    ZeroMemory(&mem,sizeof(mem));mem.dwLength=sizeof(mem);
    if(GlobalMemoryStatusEx(&mem)){
        facts.insert(QStringLiteral("memory"),formatGiB(mem.ullTotalPhys));
        facts.insert(QStringLiteral("memoryfree"),formatGiB(mem.ullAvailPhys));
        facts.insert(QStringLiteral("memoryload"),QStringLiteral("%1% in use").arg(mem.dwMemoryLoad));
    }
    WCHAR winDir[MAX_PATH];winDir[0]=0;
    if(GetWindowsDirectoryW(winDir,MAX_PATH)>0){
        const QString path=QString::fromWCharArray(winDir);
        if(path.size()>=3 && path.at(1)==QLatin1Char(':')){
            const QString root=path.left(3);
            ULARGE_INTEGER freeAvail,total,freeTotal;
            if(GetDiskFreeSpaceExW(reinterpret_cast<LPCWSTR>(root.utf16()),&freeAvail,&total,&freeTotal)){
                facts.insert(QStringLiteral("volume"),root+QStringLiteral("  ·  ")+formatGiB(total.QuadPart)+
                             QStringLiteral(" total  ·  ")+formatGiB(freeTotal.QuadPart)+QStringLiteral(" free"));
            }
        }
    }
    SYSTEM_POWER_STATUS power;
    ZeroMemory(&power,sizeof(power));
    if(GetSystemPowerStatus(&power)){
        if(power.BatteryFlag==128)
            facts.insert(QStringLiteral("battery"),QStringLiteral("No system battery reported"));
        if(power.ACLineStatus==1)facts.insert(QStringLiteral("power"),QStringLiteral("AC power"));
        else if(power.ACLineStatus==0)facts.insert(QStringLiteral("power"),QStringLiteral("Battery / offline"));
        if(power.BatteryLifePercent<=100){
            const QString left=QStringLiteral("%1% battery").arg(power.BatteryLifePercent);
            facts[QStringLiteral("power")]=facts.value(QStringLiteral("power"),QString())+
                QStringLiteral("  ·  ")+left;
        }
    }
    const QString secure=registryValue(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State"),
                                       QStringLiteral("UEFISecureBootEnabled"));
    facts.insert(QStringLiteral("secureboot"),secure==missing?QStringLiteral("Not reported"):
                 (secure==QStringLiteral("1")?QStringLiteral("Enabled"):QStringLiteral("Disabled")));
#else
    facts.insert(QStringLiteral("os"),QStringLiteral("Windows required for full local diagnostics"));
#endif

    const QRect display=QApplication::desktop()->screenGeometry();
    facts.insert(QStringLiteral("resolution"),QStringLiteral("%1 × %2  ·  %3 screen(s)")
        .arg(display.width()).arg(display.height()).arg(QApplication::desktop()->screenCount()));
#ifdef Q_OS_WIN
    DEVMODEW mode;ZeroMemory(&mode,sizeof(mode));mode.dmSize=sizeof(mode);
    if(EnumDisplaySettingsW(NULL,ENUM_CURRENT_SETTINGS,&mode) && mode.dmDisplayFrequency>1)
        facts.insert(QStringLiteral("refresh"),QStringLiteral("%1 Hz (primary display)").arg(mode.dmDisplayFrequency));
    else facts.insert(QStringLiteral("refresh"),QStringLiteral("Not reported by display driver"));
#else
    facts.insert(QStringLiteral("refresh"),QStringLiteral("Windows required"));
#endif
    if(!demo){
        facts.insert(QStringLiteral("graphics"),findDevice(devices,QStringList()<<QStringLiteral("display")));
        facts.insert(QStringLiteral("storage"),findDevice(devices,QStringList()<<QStringLiteral("diskdrive")<<QStringLiteral("hdc")<<QStringLiteral("scsiadapter")));
        facts.insert(QStringLiteral("network"),findDevice(devices,QStringList()<<QStringLiteral("net")));
        facts.insert(QStringLiteral("wireless"),findDevice(devices,QStringList(),QStringList()<<QStringLiteral("Wi-Fi")<<QStringLiteral("Wireless")<<QStringLiteral("802.11")));
        facts.insert(QStringLiteral("audio"),findDevice(devices,QStringList()<<QStringLiteral("media")));
        facts.insert(QStringLiteral("bluetooth"),findDevice(devices,QStringList()<<QStringLiteral("bluetooth")));
        facts.insert(QStringLiteral("usb"),findDevice(devices,QStringList()<<QStringLiteral("usb")));
        facts.insert(QStringLiteral("battery"),findDevice(devices,QStringList()<<QStringLiteral("battery")));
        facts.insert(QStringLiteral("tpm"),findDevice(devices,QStringList()<<QStringLiteral("securitydevices"),
            QStringList()<<QStringLiteral("Trusted Platform Module")<<QStringLiteral("TPM")));
        int problemCount=0;
        for(int i=0;i<devices.size();++i) if(devices.at(i).hasProblem)++problemCount;
        facts.insert(QStringLiteral("issues"),QStringLiteral("%1 matched devices  ·  %2 reported warnings")
                  .arg(devices.size()).arg(problemCount));
        const QMap<QString,QString> pnp=PnpProbe::presentDevices();
        if(pnp.contains(QStringLiteral("pnpCount"))) {
            const QStringList keys=QStringList()<<QStringLiteral("graphics")<<QStringLiteral("storage")
                <<QStringLiteral("network")<<QStringLiteral("wireless")<<QStringLiteral("audio")
                <<QStringLiteral("bluetooth")<<QStringLiteral("usb")<<QStringLiteral("battery")
                <<QStringLiteral("tpm");
            for(int i=0;i<keys.size();++i){
                const QString key=keys.at(i);
                if(key==QStringLiteral("battery") && facts.value(key)==QStringLiteral("No system battery reported"))continue;
                facts.insert(key,pnp.value(key));
            }
            facts[QStringLiteral("issues")]=QStringLiteral("%1 present PnP devices  ·  %2 matched  ·  %3 warnings in matches")
                .arg(pnp.value(QStringLiteral("pnpCount"))).arg(devices.size()).arg(problemCount);
        } else if(pnp.contains(QStringLiteral("probeStatus"))) {
            facts[QStringLiteral("issues")]+=QStringLiteral(" · ")+pnp.value(QStringLiteral("probeStatus"));
        }
    }else{
        const QString noMock=QStringLiteral("Demo inventory — not measured");
        const QStringList keys=QStringList()<<QStringLiteral("graphics")<<QStringLiteral("storage")
             <<QStringLiteral("network")<<QStringLiteral("wireless")<<QStringLiteral("audio")
             <<QStringLiteral("bluetooth")<<QStringLiteral("usb")<<QStringLiteral("battery")
             <<QStringLiteral("tpm")<<QStringLiteral("issues");
        for(int i=0;i<keys.size();++i)facts.insert(keys.at(i),noMock);
    }
    return facts;
}
