// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed read-only WMI queries, isolated from the UI process. No original JS/driver is run.
#include "hardware_report.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QDateTime>
#include <QMap>
#include <QSettings>
#include <cstdio>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wbemidl.h>
#include <oleauto.h>
#endif
namespace
{
struct Query
{
    const char *section, *arabic, *klass, *fields;
};
static const Query queries[] = {
    {"Computer", "الكمبيوتر", "Win32_ComputerSystem",
     "Manufacturer,Model,SystemType,Name,UserName,Domain,TotalPhysicalMemory,NumberOfLogicalProcessors"},
    {"System identity", "هوية الجهاز", "Win32_ComputerSystemProduct",
     "Vendor,Name,Version,IdentifyingNumber,UUID"},
    {"Operating system", "نظام التشغيل", "Win32_OperatingSystem",
     "Caption,Version,BuildNumber,CSDVersion,OSArchitecture,InstallDate,LastBootUpTime,LocalDateTime"},
    {"Motherboard", "اللوحة الأم", "Win32_BaseBoard", "Manufacturer,Product,Version,SerialNumber"},
    {"BIOS", "BIOS", "Win32_BIOS", "Manufacturer,Name,SMBIOSBIOSVersion,ReleaseDate,SerialNumber"},
    {"Processor", "المعالج", "Win32_Processor",
     "Name,Manufacturer,SocketDesignation,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,"
     "CurrentClockSpeed"},
    {"Memory modules", "شرائح الذاكرة", "Win32_PhysicalMemory",
     "Manufacturer,PartNumber,BankLabel,DeviceLocator,Capacity,Speed,ConfiguredClockSpeed"},
    {"Video cards", "كروت الشاشة", "Win32_VideoController",
     "Name,VideoProcessor,AdapterRAM,DriverVersion,DriverDate,CurrentHorizontalResolution,"
     "CurrentVerticalResolution,CurrentRefreshRate,PNPDeviceID"},
    {"Monitors", "الشاشات", "Win32_DesktopMonitor",
     "Name,MonitorManufacturer,MonitorType,ScreenWidth,ScreenHeight,PNPDeviceID"},
    {"Sound devices", "أجهزة الصوت", "Win32_SoundDevice", "Name,Manufacturer,Status,PNPDeviceID"},
    {"Disk drives", "أقراص التخزين", "Win32_DiskDrive",
     "Model,Manufacturer,InterfaceType,MediaType,Size,SerialNumber,FirmwareRevision,PNPDeviceID"},
    {"Volumes", "أقسام الأقراص", "Win32_LogicalDisk", "Name,VolumeName,FileSystem,Size,FreeSpace,DriveType"},
    {"Optical drives", "محركات الأقراص الضوئية", "Win32_CDROMDrive",
     "Name,Manufacturer,Drive,Status,PNPDeviceID"},
    {"Network adapters", "محولات الشبكة", "Win32_NetworkAdapter",
     "Name,Manufacturer,MACAddress,Speed,NetConnectionID,NetConnectionStatus,PNPDeviceID"},
    {"Network configuration", "إعداد الشبكة", "Win32_NetworkAdapterConfiguration",
     "Description,IPEnabled,IPAddress,DefaultIPGateway,DHCPEnabled,DNSServerSearchOrder,MACAddress"},
    {"Keyboards", "لوحات المفاتيح", "Win32_Keyboard", "Name,Description,Status,PNPDeviceID"},
    {"Pointing devices", "أجهزة التأشير", "Win32_PointingDevice", "Name,Manufacturer,Status,PNPDeviceID"},
    {"Printers", "الطابعات", "Win32_Printer", "Name,DriverName,PortName,Default,WorkOffline,Status"},
    {"USB controllers", "متحكمات USB", "Win32_USBController", "Name,Manufacturer,Status,PNPDeviceID"},
    {"Battery", "البطارية", "Win32_Battery",
     "Name,Status,BatteryStatus,EstimatedChargeRemaining,EstimatedRunTime,DesignVoltage"},
    {"Enclosure", "هيكل الجهاز", "Win32_SystemEnclosure", "Manufacturer,Model,Version,ChassisTypes"},
    {"Processes", "العمليات", "Win32_Process", "Name,ProcessId,Priority,ThreadCount,WorkingSetSize"},
    {"Services", "الخدمات", "Win32_Service", "Name,DisplayName,State,StartMode,PathName"},
    {"IDE controllers", "متحكمات IDE", "Win32_IDEController", "Name,Manufacturer,Status,PNPDeviceID"},
    {"Portable battery", "البطارية المحمولة", "Win32_PortableBattery",
     "Name,Manufacturer,Status,DesignCapacity,FullChargeCapacity,EstimatedChargeRemaining"}};
static QString propertyLabel(const QString &key, bool arabic)
{
    const QMap<QString, QString> extra = {
        {QStringLiteral("ProcessId"), QString::fromUtf8("معرّف العملية")},
        {QStringLiteral("DisplayName"), QString::fromUtf8("الاسم المعروض")},
        {QStringLiteral("State"), QString::fromUtf8("الحالة")},
        {QStringLiteral("StartMode"), QString::fromUtf8("نوع بدء التشغيل")},
        {QStringLiteral("PathName"), QString::fromUtf8("المسار")},
        {QStringLiteral("WorkingSetSize"), QString::fromUtf8("الذاكرة المستخدمة")},
        {QStringLiteral("Priority"), QString::fromUtf8("الأولوية")},
        {QStringLiteral("ThreadCount"), QString::fromUtf8("عدد سلاسل التنفيذ")},
        {QStringLiteral("DesignCapacity"), QString::fromUtf8("السعة التصميمية")},
        {QStringLiteral("FullChargeCapacity"), QString::fromUtf8("سعة الشحن الكامل")}};
    if (arabic && extra.contains(key))
        return extra.value(key);
    if (!arabic)
        return key;
    static const QMap<QString, QString> labels = {
        {QStringLiteral("Name"), QString::fromUtf8("الاسم")},
        {QStringLiteral("Manufacturer"), QString::fromUtf8("الشركة المصنّعة")},
        {QStringLiteral("Vendor"), QString::fromUtf8("الشركة")},
        {QStringLiteral("Model"), QString::fromUtf8("الطراز")},
        {QStringLiteral("Version"), QString::fromUtf8("الإصدار")},
        {QStringLiteral("SystemType"), QString::fromUtf8("نوع النظام")},
        {QStringLiteral("UserName"), QString::fromUtf8("اسم المستخدم")},
        {QStringLiteral("Domain"), QString::fromUtf8("النطاق")},
        {QStringLiteral("Caption"), QString::fromUtf8("الوصف")},
        {QStringLiteral("TotalPhysicalMemory"), QString::fromUtf8("إجمالي الذاكرة")},
        {QStringLiteral("Capacity"), QString::fromUtf8("السعة")},
        {QStringLiteral("NumberOfLogicalProcessors"), QString::fromUtf8("المعالجات المنطقية")},
        {QStringLiteral("NumberOfCores"), QString::fromUtf8("عدد الأنوية")},
        {QStringLiteral("IdentifyingNumber"), QString::fromUtf8("رقم التعريف")},
        {QStringLiteral("SerialNumber"), QString::fromUtf8("الرقم التسلسلي")},
        {QStringLiteral("UUID"), QString::fromUtf8("المعرّف الثابت")},
        {QStringLiteral("BuildNumber"), QString::fromUtf8("رقم البناء")},
        {QStringLiteral("CSDVersion"), QString::fromUtf8("حزمة الخدمة")},
        {QStringLiteral("OSArchitecture"), QString::fromUtf8("معمارية النظام")},
        {QStringLiteral("InstallDate"), QString::fromUtf8("تاريخ التثبيت")},
        {QStringLiteral("LastBootUpTime"), QString::fromUtf8("آخر تشغيل")},
        {QStringLiteral("LocalDateTime"), QString::fromUtf8("التاريخ والوقت")},
        {QStringLiteral("Product"), QString::fromUtf8("المنتج")},
        {QStringLiteral("SMBIOSBIOSVersion"), QString::fromUtf8("إصدار BIOS")},
        {QStringLiteral("ReleaseDate"), QString::fromUtf8("تاريخ الإصدار")},
        {QStringLiteral("SocketDesignation"), QString::fromUtf8("مقبس المعالج")},
        {QStringLiteral("MaxClockSpeed"), QString::fromUtf8("أقصى تردد مُبلغ")},
        {QStringLiteral("CurrentClockSpeed"), QString::fromUtf8("التردد المُبلغ")},
        {QStringLiteral("ConfiguredClockSpeed"), QString::fromUtf8("تردد الذاكرة المضبوط")},
        {QStringLiteral("PartNumber"), QString::fromUtf8("رقم القطعة")},
        {QStringLiteral("BankLabel"), QString::fromUtf8("بنك الذاكرة")},
        {QStringLiteral("DeviceLocator"), QString::fromUtf8("مكان الشريحة")},
        {QStringLiteral("VideoProcessor"), QString::fromUtf8("معالج الرسوم")},
        {QStringLiteral("AdapterRAM"), QString::fromUtf8("ذاكرة الرسوم المُبلغة من WMI")},
        {QStringLiteral("DriverVersion"), QString::fromUtf8("إصدار التعريف")},
        {QStringLiteral("DriverDate"), QString::fromUtf8("تاريخ التعريف")},
        {QStringLiteral("PNPDeviceID"), QString::fromUtf8("معرّف الجهاز")},
        {QStringLiteral("CurrentHorizontalResolution"), QString::fromUtf8("العرض الحالي")},
        {QStringLiteral("CurrentVerticalResolution"), QString::fromUtf8("الارتفاع الحالي")},
        {QStringLiteral("CurrentRefreshRate"), QString::fromUtf8("معدل التحديث")},
        {QStringLiteral("MonitorManufacturer"), QString::fromUtf8("مصنّع الشاشة")},
        {QStringLiteral("MonitorType"), QString::fromUtf8("نوع الشاشة")},
        {QStringLiteral("ScreenWidth"), QString::fromUtf8("عرض الشاشة")},
        {QStringLiteral("ScreenHeight"), QString::fromUtf8("ارتفاع الشاشة")},
        {QStringLiteral("Status"), QString::fromUtf8("الحالة")},
        {QStringLiteral("InterfaceType"), QString::fromUtf8("نوع الاتصال")},
        {QStringLiteral("MediaType"), QString::fromUtf8("نوع الوسيط")},
        {QStringLiteral("Size"), QString::fromUtf8("الحجم")},
        {QStringLiteral("FirmwareRevision"), QString::fromUtf8("البرنامج الثابت")},
        {QStringLiteral("FreeSpace"), QString::fromUtf8("المساحة الحرة")},
        {QStringLiteral("VolumeName"), QString::fromUtf8("اسم القسم")},
        {QStringLiteral("FileSystem"), QString::fromUtf8("نظام الملفات")},
        {QStringLiteral("DriveType"), QString::fromUtf8("نوع القرص")},
        {QStringLiteral("Drive"), QString::fromUtf8("القرص")},
        {QStringLiteral("Description"), QString::fromUtf8("الوصف")},
        {QStringLiteral("MACAddress"), QString::fromUtf8("عنوان MAC")},
        {QStringLiteral("Speed"), QString::fromUtf8("السرعة المُبلغة")},
        {QStringLiteral("NetConnectionID"), QString::fromUtf8("اسم الاتصال")},
        {QStringLiteral("NetConnectionStatus"), QString::fromUtf8("حالة الاتصال المُبلغة")},
        {QStringLiteral("IPEnabled"), QString::fromUtf8("تفعيل IP")},
        {QStringLiteral("IPAddress"), QString::fromUtf8("عناوين IP")},
        {QStringLiteral("DefaultIPGateway"), QString::fromUtf8("البوابة الافتراضية")},
        {QStringLiteral("DHCPEnabled"), QString::fromUtf8("تفعيل DHCP")},
        {QStringLiteral("DNSServerSearchOrder"), QString::fromUtf8("خوادم DNS")},
        {QStringLiteral("DriverName"), QString::fromUtf8("اسم التعريف")},
        {QStringLiteral("PortName"), QString::fromUtf8("المنفذ")},
        {QStringLiteral("Default"), QString::fromUtf8("افتراضي")},
        {QStringLiteral("WorkOffline"), QString::fromUtf8("العمل دون اتصال")},
        {QStringLiteral("BatteryStatus"), QString::fromUtf8("حالة البطارية")},
        {QStringLiteral("EstimatedChargeRemaining"), QString::fromUtf8("الشحن المتبقي المُبلغ")},
        {QStringLiteral("EstimatedRunTime"), QString::fromUtf8("الوقت المتبقي المُبلغ")},
        {QStringLiteral("DesignVoltage"), QString::fromUtf8("الجهد التصميمي")},
        {QStringLiteral("ChassisTypes"), QString::fromUtf8("نوع الهيكل")}};
    return labels.value(key, key);
}
#ifdef Q_OS_WIN
struct Bstr
{
    BSTR value;
    explicit Bstr(const QString &s) : value(SysAllocString(reinterpret_cast<const OLECHAR *>(s.utf16()))) {}
    ~Bstr() { SysFreeString(value); }
};
template <class T> struct Com
{
    T *value = 0;
    ~Com()
    {
        if (value)
            value->Release();
    }
};
static QString variantText(VARIANT &v)
{
    if (v.vt == VT_EMPTY || v.vt == VT_NULL)
        return QString();
    if (v.vt & VT_ARRAY)
    {
        if (!v.parray || SafeArrayGetDim(v.parray) != 1)
            return QString();
        LONG first = 0, last = -1;
        SafeArrayGetLBound(v.parray, 1, &first);
        SafeArrayGetUBound(v.parray, 1, &last);
        QStringList values;
        for (LONG i = first; i <= last && i < first + 64; ++i)
        {
            const VARTYPE type = v.vt & VT_TYPEMASK;
            if (type == VT_BSTR)
            {
                BSTR text = 0;
                if (SUCCEEDED(SafeArrayGetElement(v.parray, &i, &text)) && text)
                {
                    values << QString::fromWCharArray(text);
                    SysFreeString(text);
                }
            }
            else if (type == VT_VARIANT)
            {
                VARIANT item;
                VariantInit(&item);
                if (SUCCEEDED(SafeArrayGetElement(v.parray, &i, &item)))
                    values << variantText(item);
                VariantClear(&item);
            }
            else if (type == VT_UI2)
            {
                USHORT item = 0;
                if (SUCCEEDED(SafeArrayGetElement(v.parray, &i, &item)))
                    values << QString::number(item);
            }
            else if (type == VT_I4 || type == VT_UI4)
            {
                LONG item = 0;
                if (SUCCEEDED(SafeArrayGetElement(v.parray, &i, &item)))
                    values << QString::number(item);
            }
        }
        return values.join(QStringLiteral(" · ")).left(4096);
    }
    if (v.vt == VT_BOOL)
        return v.boolVal ? QStringLiteral("True") : QStringLiteral("False");
    VARIANT text;
    VariantInit(&text);
    QString result;
    if (SUCCEEDED(VariantChangeType(&text, &v, 0, VT_BSTR)) && text.bstrVal)
        result = QString::fromWCharArray(text.bstrVal).left(4096);
    VariantClear(&text);
    return result;
}
static QString display(const QString &key, QString value)
{
    if (key == QStringLiteral("WorkingSetSize"))
    {
        bool ok = false;
        const double size = value.toDouble(&ok);
        if (ok)
            return QStringLiteral("%1 MB").arg(size / 1048576, 0, 'f', 1);
    }
    if ((key.contains(QStringLiteral("Date")) || key == QStringLiteral("LastBootUpTime")) &&
        value.size() >= 14)
    {
        const auto date = QDateTime::fromString(value.left(14), QStringLiteral("yyyyMMddHHmmss"));
        if (date.isValid())
            return date.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    const QStringList bytes = QStringList() << QStringLiteral("TotalPhysicalMemory")
                                            << QStringLiteral("Capacity") << QStringLiteral("AdapterRAM")
                                            << QStringLiteral("Size") << QStringLiteral("FreeSpace");
    if (bytes.contains(key))
    {
        bool ok = false;
        const double size = value.toDouble(&ok);
        if (ok && size > 0)
            return QStringLiteral("%1 GB").arg(size / 1024 / 1024 / 1024, 0, 'f', 2);
        if (key == QStringLiteral("AdapterRAM"))
            return QString();
    }
    if (key == QStringLiteral("CurrentClockSpeed") || key == QStringLiteral("MaxClockSpeed") ||
        key == QStringLiteral("ConfiguredClockSpeed"))
        return value + QStringLiteral(" MHz");
    return value;
}
static QJsonObject readSystemErrors()
{
    QJsonObject result;
    QJsonArray errors;
    HANDLE log = OpenEventLogW(0, L"System");
    if (!log)
    {
        result.insert(QStringLiteral("error"), int(GetLastError()));
        return result;
    }
    struct CloseLog
    {
        HANDLE value;
        ~CloseLog() { CloseEventLog(value); }
    } closed{log};
    QByteArray data(65536, 0);
    const qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000;
    int day = 0, week = 0, readBytes = 0;
    bool finished = false;
    while (readBytes < 8 * 1024 * 1024 && !finished)
    {
        DWORD actual = 0, needed = 0;
        if (!ReadEventLogW(log, EVENTLOG_BACKWARDS_READ | EVENTLOG_SEQUENTIAL_READ, 0, data.data(),
                           data.size(), &actual, &needed))
            break;
        readBytes += actual;
        DWORD offset = 0;
        while (offset + sizeof(EVENTLOGRECORD) <= actual)
        {
            const auto record = reinterpret_cast<const EVENTLOGRECORD *>(data.constData() + offset);
            if (record->Length < sizeof(EVENTLOGRECORD) || record->Length > actual - offset)
            {
                finished = true;
                break;
            }
            if (qint64(record->TimeGenerated) < now - 7 * 86400)
            {
                finished = true;
                break;
            }
            if (record->EventType == EVENTLOG_ERROR_TYPE)
            {
                ++week;
                if (qint64(record->TimeGenerated) >= now - 86400)
                    ++day;
                if (errors.size() < 30)
                {
                    QJsonObject item;
                    const WCHAR *source = reinterpret_cast<const WCHAR *>(
                        reinterpret_cast<const char *>(record) + sizeof(EVENTLOGRECORD));
                    int count = 0, limit = (record->Length - sizeof(EVENTLOGRECORD)) / sizeof(WCHAR);
                    while (count < limit && source[count])
                        ++count;
                    item.insert(QStringLiteral("source"), QString::fromWCharArray(source, count));
                    item.insert(QStringLiteral("eventId"), int(record->EventID & 0xffff));
                    item.insert(QStringLiteral("time"),
                                QDateTime::fromMSecsSinceEpoch(qint64(record->TimeGenerated) * 1000)
                                    .toString(Qt::ISODate));
                    errors.append(item);
                }
            }
            offset += record->Length;
        }
    }
    result.insert(QStringLiteral("last24Hours"), day);
    result.insert(QStringLiteral("last7Days"), week);
    result.insert(QStringLiteral("boundedRead"), !finished);
    result.insert(QStringLiteral("errors"), errors);
    return result;
}
static QJsonObject securityQuery(IWbemLocator *locator, const QString &space, const QString &klass,
                                 const QStringList &fields)
{
    QJsonObject result;
    QJsonArray rows;
    Com<IWbemServices> service;
    Bstr root(space);
    HRESULT status =
        locator->ConnectServer(root.value, 0, 0, 0, WBEM_FLAG_CONNECT_USE_MAX_WAIT, 0, 0, &service.value);
    if (SUCCEEDED(status))
    {
        CoSetProxyBlanket(service.value, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, 0, RPC_C_AUTHN_LEVEL_CALL,
                          RPC_C_IMP_LEVEL_IMPERSONATE, 0, EOAC_NONE);
        Bstr language(QStringLiteral("WQL"));
        Bstr query(QStringLiteral("SELECT %1 FROM %2").arg(fields.join(QLatin1Char(',')), klass));
        Com<IEnumWbemClassObject> enumeration;
        status = service.value->ExecQuery(language.value, query.value,
                                          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, 0,
                                          &enumeration.value);
        if (SUCCEEDED(status))
        {
            QElapsedTimer limit;
            limit.start();
            while (rows.size() < 32 && limit.elapsed() < 1500)
            {
                Com<IWbemClassObject> item;
                ULONG returned = 0;
                status = enumeration.value->Next(400, 1, &item.value, &returned);
                if (status == WBEM_S_TIMEDOUT || FAILED(status) || !returned)
                    break;
                QJsonObject values;
                for (const auto &field : fields)
                {
                    VARIANT value;
                    VariantInit(&value);
                    if (SUCCEEDED(item.value->Get(reinterpret_cast<LPCWSTR>(field.utf16()), 0, &value, 0, 0)))
                    {
                        if (value.vt == VT_BOOL)
                            values.insert(field, value.boolVal != VARIANT_FALSE);
                        else if (value.vt != VT_EMPTY && value.vt != VT_NULL)
                            values.insert(field, variantText(value));
                    }
                    VariantClear(&value);
                }
                rows.append(values);
            }
        }
    }
    result.insert(QStringLiteral("available"), !rows.isEmpty());
    result.insert(QStringLiteral("status"),
                  QStringLiteral("0x%1").arg(quint32(status), 8, 16, QLatin1Char('0')));
    result.insert(QStringLiteral("items"), rows);
    return result;
}
static QJsonObject readSecurity(IWbemLocator *locator, bool allowWmi)
{
    QJsonObject result;
    if (allowWmi)
    {
        result.insert(QStringLiteral("defender"),
                      securityQuery(locator, QStringLiteral("ROOT\\Microsoft\\Windows\\Defender"),
                                    QStringLiteral("MSFT_MpComputerStatus"),
                                    QStringList() << QStringLiteral("AntivirusEnabled")
                                                  << QStringLiteral("RealTimeProtectionEnabled")
                                                  << QStringLiteral("AntivirusSignatureLastUpdated")
                                                  << QStringLiteral("QuickScanEndTime")));
        result.insert(QStringLiteral("antivirus"),
                      securityQuery(locator, QStringLiteral("ROOT\\SecurityCenter2"),
                                    QStringLiteral("AntiVirusProduct"),
                                    QStringList() << QStringLiteral("displayName")));
    }
    CLSID policyClass = {};
    Com<IDispatch> policy;
    if (SUCCEEDED(
            CLSIDFromString(const_cast<LPOLESTR>(L"{E2B3C97F-6AE1-41AC-817A-F6F92166D7DD}"), &policyClass)) &&
        SUCCEEDED(CoCreateInstance(policyClass, 0, CLSCTX_INPROC_SERVER, IID_IDispatch,
                                   reinterpret_cast<void **>(&policy.value))))
    {
        LPOLESTR name = const_cast<LPOLESTR>(L"FirewallEnabled");
        DISPID id = 0;
        if (SUCCEEDED(policy.value->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_SYSTEM_DEFAULT, &id)))
            for (int profile : {1, 2, 4})
            {
                VARIANT argument, value;
                VariantInit(&argument);
                VariantInit(&value);
                argument.vt = VT_I4;
                argument.lVal = profile;
                DISPPARAMS arguments = {&argument, 0, 1, 0};
                if (SUCCEEDED(policy.value->Invoke(id, IID_NULL, LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYGET,
                                                   &arguments, &value, 0, 0)) &&
                    value.vt == VT_BOOL)
                    result.insert(QStringLiteral("firewall%1").arg(profile), value.boolVal != VARIANT_FALSE);
                VariantClear(&value);
            }
    }
    QSettings uac(
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System"),
        QSettings::NativeFormat);
    if (uac.contains(QStringLiteral("EnableLUA")))
        result.insert(QStringLiteral("uacConfigured"), uac.value(QStringLiteral("EnableLUA")).toUInt() != 0);
    QSettings boot(
        QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State"),
        QSettings::NativeFormat);
    if (boot.contains(QStringLiteral("UEFISecureBootEnabled")))
        result.insert(QStringLiteral("secureBoot"),
                      boot.value(QStringLiteral("UEFISecureBootEnabled")).toUInt() != 0);
    result.insert(QStringLiteral("readOnly"), true);
    return result;
}
static QJsonObject collect()
{
    const HRESULT initialized = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        throw std::runtime_error("WMI COM initialization failed.");
    struct End
    {
        ~End() { CoUninitialize(); }
    } end;
    CoInitializeSecurity(0, -1, 0, 0, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, 0, EOAC_NONE,
                         0);
    const CLSID locatorClass = {0x4590f811, 0x1d3a, 0x11d0, {0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24}};
    Com<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(locatorClass, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                reinterpret_cast<void **>(&locator.value))))
        throw std::runtime_error("Windows WMI service is unavailable.");
    Com<IWbemServices> service;
    Bstr root(QStringLiteral("ROOT\\CIMV2"));
    if (FAILED(locator.value->ConnectServer(root.value, 0, 0, 0, WBEM_FLAG_CONNECT_USE_MAX_WAIT, 0, 0,
                                            &service.value)))
        throw std::runtime_error("Cannot connect to local WMI.");
    CoSetProxyBlanket(service.value, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, 0, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, 0, EOAC_NONE);
    QJsonArray sections;
    QElapsedTimer total;
    total.start();
    for (const Query &query : queries)
    {
        if (total.elapsed() > 14000)
            break;
        QJsonObject section;
        section.insert(QStringLiteral("name"), QString::fromUtf8(query.section));
        section.insert(QStringLiteral("arabic"), QString::fromUtf8(query.arabic));
        section.insert(QStringLiteral("class"), QString::fromLatin1(query.klass));
        QJsonArray items;
        Bstr language(QStringLiteral("WQL")),
            command((QString::fromLatin1(query.klass) == QStringLiteral("Win32_VideoController")
                         ? QStringLiteral("SELECT ") + QString::fromLatin1(query.fields) +
                               QStringLiteral(" FROM ")
                         : QStringLiteral("SELECT * FROM ")) +
                    QString::fromLatin1(query.klass));
        Com<IEnumWbemClassObject> enumerator;
        const HRESULT status = service.value->ExecQuery(language.value, command.value,
                                                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                                        0, &enumerator.value);
        section.insert(QStringLiteral("queryStatus"),
                       QStringLiteral("0x%1").arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0')));
        if (SUCCEEDED(status))
        {
            QElapsedTimer clock;
            clock.start();
            while (clock.elapsed() <
                       (QString::fromLatin1(query.klass) == QStringLiteral("Win32_VideoController") ? 4500
                                                                                                    : 1700) &&
                   total.elapsed() < 14000 && items.size() < 1024)
            {
                Com<IWbemClassObject> item;
                ULONG returned = 0;
                HRESULT next = enumerator.value->Next(200, 1, &item.value, &returned);
                section.insert(
                    QStringLiteral("nextStatus"),
                    QStringLiteral("0x%1").arg(static_cast<quint32>(next), 8, 16, QLatin1Char('0')));
                if (next == WBEM_S_TIMEDOUT)
                    continue;
                if (FAILED(next) || returned == 0)
                    break;
                QJsonObject values;
                for (const QString &property : QString::fromLatin1(query.fields).split(QLatin1Char(',')))
                {
                    VARIANT v;
                    VariantInit(&v);
                    if (SUCCEEDED(item.value->Get(reinterpret_cast<LPCWSTR>(property.utf16()), 0, &v, 0, 0)))
                    {
                        const QString text = display(property, variantText(v));
                        if (!text.isEmpty())
                            values.insert(property, text);
                    }
                    VariantClear(&v);
                }
                if (!values.isEmpty())
                    items.append(values);
            }
        }
        section.insert(QStringLiteral("truncated"), items.size() >= 1024);
        section.insert(QStringLiteral("items"), items);
        section.insert(QStringLiteral("available"), !items.isEmpty());
        sections.append(section);
    }
    QJsonObject result;
    result.insert(QStringLiteral("format"), QStringLiteral("DriverPackModernHardwareReport"));
    result.insert(QStringLiteral("sections"), sections);
    result.insert(QStringLiteral("expectedSections"), int(sizeof(queries) / sizeof(queries[0])));
    bool complete = sections.size() == int(sizeof(queries) / sizeof(queries[0]));
    for (const auto &entry : sections)
    {
        const auto section = entry.toObject();
        const QString query = section.value(QStringLiteral("queryStatus")).toString(),
                      next = section.value(QStringLiteral("nextStatus")).toString();
        if (section.value(QStringLiteral("truncated")).toBool() || query != QStringLiteral("0x00000000") ||
            (next != QStringLiteral("0x00000000") && next != QStringLiteral("0x00000001")))
            complete = false;
    }
    result.insert(QStringLiteral("complete"), complete);
    result.insert(QStringLiteral("eventLog"), readSystemErrors());
    result.insert(QStringLiteral("security"), readSecurity(locator.value, total.elapsed() < 12000));
    result.insert(QStringLiteral("readOnly"), true);
    result.insert(QStringLiteral("milliseconds"), static_cast<double>(total.elapsed()));
    result.insert(
        QStringLiteral("temperature"),
        QStringLiteral(
            "No raw sensor driver was loaded. Temperature requires an existing compatible sensor provider."));
    return result;
}
#endif
} // namespace
QString hardwarePropertyLabel(const QString &key, bool arabic) { return propertyLabel(key, arabic); }
int runHardwareReport(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        const QStringList args = app.arguments();
        const QString output = args.value(args.indexOf(QStringLiteral("--hardware-report")) + 1);
        if (output.isEmpty())
            throw std::runtime_error("Output path required.");
#ifdef Q_OS_WIN
        const QJsonObject report = collect();
        QSaveFile file(output);
        const QByteArray bytes = QJsonDocument(report).toJson();
        if (bytes.size() > 2 * 1024 * 1024 || !file.open(QIODevice::WriteOnly) ||
            file.write(bytes) != bytes.size() || !file.commit())
            throw std::runtime_error("Cannot save the bounded local hardware report.");
        return 0;
#else
        throw std::runtime_error("Windows WMI is required.");
#endif
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Hardware diagnostics: %s\n", e.what());
        return 1;
    }
}
void HardwareWorker::run()
{
    result = QJsonObject();
    error.clear();
    QTemporaryDir temp;
    if (!temp.isValid())
    {
        error = QStringLiteral("Cannot create a local diagnostic workspace.");
        return;
    }
    const QString output = QDir(temp.path()).filePath(QStringLiteral("hardware.json"));
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(),
                  QStringList() << QStringLiteral("--hardware-report") << output);
    if (!process.waitForStarted(1000))
    {
        error = QStringLiteral("Cannot start read-only hardware diagnostics.");
        return;
    }
    QElapsedTimer limit;
    limit.start();
    while (!process.waitForFinished(100))
    {
        if (stop.load() || limit.elapsed() > 18000)
        {
            process.kill();
            process.waitForFinished(1000);
            error = stop.load() ? QStringLiteral("Hardware diagnostics cancelled.")
                                : QStringLiteral("WMI timed out. The interface remains available.");
            return;
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        error = QString::fromUtf8(process.readAllStandardError()).left(2048);
        return;
    }
    QFile file(output);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
    {
        error = QStringLiteral("Invalid hardware report.");
        return;
    }
    result = QJsonDocument::fromJson(file.readAll()).object();
    if (result.value(QStringLiteral("format")).toString() != QStringLiteral("DriverPackModernHardwareReport"))
    {
        result = QJsonObject();
        error = QStringLiteral("Unsupported hardware report format.");
    }
}
