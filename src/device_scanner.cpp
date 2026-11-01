#include "device_scanner.h"
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

static QString deviceProperty(HDEVINFO info, SP_DEVINFO_DATA *device, DWORD property)
{
    WCHAR buffer[2048];
    DWORD type = 0;
    DWORD required = 0;
    ZeroMemory(buffer, sizeof(buffer));
    if (!SetupDiGetDeviceRegistryPropertyW(info, device, property, &type,
            reinterpret_cast<PBYTE>(buffer), sizeof(buffer), &required))
        return QString();
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
        return QString();
    buffer[2047] = 0;
    return QString::fromWCharArray(buffer).trimmed();
}

static QString driverVersion(HDEVINFO info, SP_DEVINFO_DATA *device)
{
    HKEY key = SetupDiOpenDevRegKey(info, device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE)
        return QString();
    WCHAR buffer[256];
    DWORD type = 0;
    DWORD len = sizeof(buffer);
    QString version;
    if (RegQueryValueExW(key, L"DriverVersion", 0, &type,
            reinterpret_cast<LPBYTE>(buffer), &len) == ERROR_SUCCESS && type == REG_SZ) {
        buffer[255] = 0;
        version = QString::fromWCharArray(buffer).trimmed();
    }
    RegCloseKey(key);
    return version;
}

static QVector<DeviceInfo> listWindowsDevices(QString *message)
{
    QVector<DeviceInfo> result;
    HDEVINFO info = SetupDiGetClassDevsW(0, 0, 0, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (info == INVALID_HANDLE_VALUE) {
        *message = QStringLiteral("Windows SetupAPI failed: %1").arg(GetLastError());
        return result;
    }

    for (DWORD i = 0; i < 8192; ++i) {
        SP_DEVINFO_DATA data;
        ZeroMemory(&data, sizeof(data));
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(info, i, &data)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                break;
            continue;
        }

        DeviceInfo item;
        item.name = deviceProperty(info, &data, SPDRP_FRIENDLYNAME);
        if (item.name.isEmpty())
            item.name = deviceProperty(info, &data, SPDRP_DEVICEDESC);
        if (item.name.isEmpty())
            continue;
        item.manufacturer = deviceProperty(info, &data, SPDRP_MFG);
        item.deviceClass = deviceProperty(info, &data, SPDRP_CLASS);
        item.hardwareId = deviceProperty(info, &data, SPDRP_HARDWAREID);
        item.driverVersion = driverVersion(info, &data);

        WCHAR id[2048];
        DWORD needed = 0;
        ZeroMemory(id, sizeof(id));
        if (SetupDiGetDeviceInstanceIdW(info, &data, id, 2048, &needed))
            item.instanceId = QString::fromWCharArray(id);

        ULONG status = 0;
        ULONG problem = 0;
        CONFIGRET res = CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0);
        item.hasProblem = (res == CR_SUCCESS) && ((status & DN_HAS_PROBLEM) != 0);
        item.problemCode = item.hasProblem ? static_cast<unsigned int>(problem) : 0;
        result.append(item);
    }

    SetupDiDestroyDeviceInfoList(info);
    std::stable_sort(result.begin(), result.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
        if (a.hasProblem != b.hasProblem)
            return a.hasProblem;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    *message = QStringLiteral("Read-only Windows PnP inventory. Driver matching not connected.");
    return result;
}
#endif

static QVector<DeviceInfo> demoDevices()
{
    QVector<DeviceInfo> result;
    const char *names[] = {"Realtek PCIe GbE Family Controller", "High Definition Audio Device",
        "Generic PnP Monitor", "Intel(R) USB 3.0 eXtensible Host Controller",
        "PCI Simple Communications Controller", "Standard SATA AHCI Controller",
        "Microsoft ACPI-Compliant Control Method Battery", "Intel(R) UHD Graphics"};
    const char *groups[] = {"Net", "MEDIA", "Monitor", "USB", "System", "HDC", "Battery", "Display"};
    for (int i = 0; i < 8; ++i) {
        DeviceInfo d;
        d.name = QString::fromLatin1(names[i]);
        d.deviceClass = QString::fromLatin1(groups[i]);
        d.manufacturer = i == 0 ? QStringLiteral("Realtek") : QStringLiteral("Example manufacturer");
        d.driverVersion = i == 4 ? QString() : QStringLiteral("10.0.19041.1");
        d.instanceId = QStringLiteral("DEMO\\DEVICE_%1").arg(i);
        d.hardwareId = QStringLiteral("DEMO\\HARDWARE_%1").arg(i);
        d.hasProblem = (i == 4);
        d.problemCode = d.hasProblem ? 28 : 0;
        result.append(d);
    }
    return result;
}

DeviceScanner::DeviceScanner(QObject *parent) : QThread(parent), m_demo(false) {}
void DeviceScanner::setDemo(bool demo) { m_demo = demo; }

void DeviceScanner::run()
{
    if (m_demo) {
        emit inventoryReady(demoDevices(), QStringLiteral("DEMO DATA: no driver actions are available."));
        return;
    }
#ifdef Q_OS_WIN
    QString message;
    QVector<DeviceInfo> items = listWindowsDevices(&message);
    emit inventoryReady(items, message);
#else
    emit inventoryReady(QVector<DeviceInfo>(), QStringLiteral("Native PnP inventory requires Windows. Use --demo to preview."));
#endif
}
