#include "pnp_probe.h"
#include <QStringList>
#include <QSet>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>
#endif

#ifdef Q_OS_WIN
static QString property(HDEVINFO list,SP_DEVINFO_DATA &data,DWORD propertyCode)
{
    WCHAR buffer[2048]={0};DWORD type=0,needed=0;
    if(!SetupDiGetDeviceRegistryPropertyW(list,&data,propertyCode,&type,
         reinterpret_cast<PBYTE>(buffer),sizeof(buffer),&needed))return QString();
    if(type!=REG_SZ && type!=REG_EXPAND_SZ && type!=REG_MULTI_SZ)return QString();
    return QString::fromWCharArray(buffer).trimmed();
}
static void push(QMap<QString,QStringList> &groups,QMap<QString,QSet<QString> > &seen,
                 const QString &key,const QString &name)
{
    const QString clean=name.trimmed();
    if(clean.isEmpty() || seen[key].contains(clean.toLower()))return;
    seen[key].insert(clean.toLower());
    groups[key].append(clean);
}
#endif
QMap<QString,QString> PnpProbe::presentDevices()
{
    QMap<QString,QString> result;
#ifdef Q_OS_WIN
    HDEVINFO handle=SetupDiGetClassDevsW(NULL,NULL,NULL,DIGCF_PRESENT|DIGCF_ALLCLASSES);
    if(handle==INVALID_HANDLE_VALUE){
        result.insert(QStringLiteral("probeStatus"),QStringLiteral("Windows SetupAPI enumeration failed"));
        return result;
    }
    QMap<QString,QStringList> groups;
    QMap<QString,QSet<QString> > seen;
    int devices=0;
    for(DWORD index=0;;++index){
        SP_DEVINFO_DATA info={0};info.cbSize=sizeof(info);
        if(!SetupDiEnumDeviceInfo(handle,index,&info)){
            if(GetLastError()!=ERROR_NO_MORE_ITEMS)
                result.insert(QStringLiteral("probeStatus"),QStringLiteral("Windows PnP inventory incomplete"));
            break;
        }
        ++devices;
        QString name=property(handle,info,SPDRP_FRIENDLYNAME);
        if(name.isEmpty())name=property(handle,info,SPDRP_DEVICEDESC);
        const QString cls=property(handle,info,SPDRP_CLASS).toLower();
        const QString low=name.toLower();
        if(cls==QStringLiteral("display"))push(groups,seen,QStringLiteral("graphics"),name);
        if(cls==QStringLiteral("diskdrive") || cls==QStringLiteral("hdc") || cls==QStringLiteral("scsiadapter"))
            push(groups,seen,QStringLiteral("storage"),name);
        if(cls==QStringLiteral("net")){
            push(groups,seen,QStringLiteral("network"),name);
            if(low.contains(QStringLiteral("wi-fi")) || low.contains(QStringLiteral("wifi")) ||
               low.contains(QStringLiteral("wireless")) || low.contains(QStringLiteral("wlan")) ||
               low.contains(QStringLiteral("802.11")))
                push(groups,seen,QStringLiteral("wireless"),name);
        }
        if(cls==QStringLiteral("bluetooth"))push(groups,seen,QStringLiteral("bluetooth"),name);
        if(cls==QStringLiteral("media"))push(groups,seen,QStringLiteral("audio"),name);
        if(cls==QStringLiteral("usb") || cls==QStringLiteral("usbdevice"))push(groups,seen,QStringLiteral("usb"),name);
        if(cls==QStringLiteral("battery"))push(groups,seen,QStringLiteral("battery"),name);
        if(cls==QStringLiteral("securitydevices") || low.contains(QStringLiteral("trusted platform module")))
            push(groups,seen,QStringLiteral("tpm"),name);
    }
    SetupDiDestroyDeviceInfoList(handle);
    result.insert(QStringLiteral("pnpCount"),QString::number(devices));
    const QStringList names=QStringList()<<QStringLiteral("graphics")<<QStringLiteral("storage")
        <<QStringLiteral("network")<<QStringLiteral("wireless")<<QStringLiteral("audio")
        <<QStringLiteral("bluetooth")<<QStringLiteral("usb")<<QStringLiteral("battery")<<QStringLiteral("tpm");
    for(int i=0;i<names.size();++i){
        const QString key=names.at(i);const QStringList values=groups.value(key);
        result.insert(key,values.isEmpty()?QStringLiteral("Not detected among present PnP devices"):
              values.mid(0,3).join(QStringLiteral(" · "))+
              (values.size()>3?QStringLiteral(" (+%1 more)").arg(values.size()-3):QString()));
    }
#else
    result.insert(QStringLiteral("probeStatus"),QStringLiteral("Windows SetupAPI required"));
#endif
    return result;
}
