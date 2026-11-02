// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SDIOMODERN_ORIGIN_API_H
#define SDIOMODERN_ORIGIN_API_H
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sdiomodern {
std::uint32_t initializeRestoreCallbacks();
// No Qt objects or upstream pointers cross this boundary. All strings and records
// are owned copies. OriginCore must only be called from its single worker thread.
enum class Stage { Scan, Indexes, Match, RestorePoint, Extract, Install, Finished };
enum class Outcome { Pending, Running, Success, Failure, Skipped, Cancelled };
struct Capabilities {
    bool nativeCore, matching, localInstall, restorePoint, snapshots, downloads;
    bool instanceInstall = false;
    Capabilities() : nativeCore(false), matching(false), localInstall(false),
        restorePoint(false), snapshots(false), downloads(false) {}
};
struct DriverData {
    std::wstring version, date, provider, inf, section, hardwareId, packageName;
};
struct Candidate : DriverData {
    std::uint64_t token;
    std::uint32_t statusBits, rawScore;
    double catalogRank=0;int minimumBuild=0,signature=0;
    int originalOrder;
    bool valid, local, recommended;
    std::wstring packagePath, reason;
    std::vector<std::uint64_t> filterVisibility;
    Candidate() : token(0), statusBits(0), rawScore(0), originalOrder(0),
        valid(false), local(false), recommended(false), filterVisibility(64,0) {}
};
struct Device {
    std::wstring instanceId, name, manufacturer, category;
    std::vector<std::wstring> hardwareIds, compatibleIds;
    std::uint32_t problemCode, matchStatus;
    bool hasInstalledDriver;
    DriverData installed;
    std::vector<Candidate> candidates;
    Device() : problemCode(0), matchStatus(0), hasInstalledDriver(false) {}
};
struct Snapshot {
    std::uint64_t generation;
    Capabilities capabilities;
    std::wstring sourceRevision;
    std::vector<Device> devices;
    int matchedWindowsVersion=0,matchedArchitecture=0;
    Snapshot() : generation(0) {}
};
struct Selection { std::wstring deviceInstanceId; std::uint64_t candidateToken; };
struct InstallPlan {
    std::uint64_t generation;
    std::vector<Selection> selections;
    bool createRestorePoint, confirmed, allowAlternatives,continueOnRestoreFailure=false;
    InstallPlan() : generation(0), createRestorePoint(true), confirmed(false),
        allowAlternatives(false) {}
};
struct Event {
    Stage stage;
    Outcome outcome;
    std::wstring deviceId, label, message;
    std::uint64_t done, total;
    std::uint32_t nativeError;
    int percent; // -1 = indeterminate. Never manufacture a percentage using time.
    bool rebootRequired;
    Event() : stage(Stage::Scan), outcome(Outcome::Running), done(0), total(0),
        nativeError(0), percent(-1), rebootRequired(false) {}
};
struct Result {
    std::wstring deviceId, name, message;
    Outcome outcome;
    std::uint32_t nativeError;
    bool rebootRequired;
    Result() : outcome(Outcome::Pending), nativeError(0), rebootRequired(false) {}
};
struct InstallReport {
    std::vector<Result> items;
    bool restoreCreated, cancelled, rebootRequired, restoreFailed;
    std::uint32_t restoreError;
    std::wstring error;
    InstallReport() : restoreCreated(false), cancelled(false), rebootRequired(false),restoreFailed(false),restoreError(0) {}
};
struct Paths {
    std::wstring drivers,indexes,logDirectory,snapshotFile,indexTextDirectory;
    int targetVersion=0,targetArchitecture=0,verbosity=-1;
    bool forceReindex=false,filterServicePacks=false,failSafe=false,deleteExtraInf=false,dpInst=false,keepTempFiles=false,noLog=false,keepUnpackedIndex=false,disableInstall=false;
};
typedef std::function<void(const Event&)> EventSink;

// Blocking interface, used by OriginWorker, not by any QWidget.
class OriginCore {
public:
    OriginCore();
    ~OriginCore();
    Capabilities capabilities() const;
    Snapshot scan(const Paths&, const EventSink&);
    InstallReport install(const InstallPlan&, const EventSink&, const std::atomic<bool>& stopAfterCurrent);
    InstallReport preflight(const InstallPlan&, const EventSink&, const std::atomic<bool>&);
    void saveSnapshot(const std::wstring& file);
    std::wstring extract(const InstallPlan&,const std::wstring& folder,const EventSink&,const std::atomic<bool>& stop);
    void createRestorePoint(const std::wstring &description=std::wstring());
    void exportIndexText(const std::wstring& folder);
    void writeDeviceList(const std::wstring& file);
    InstallReport installInf(const std::wstring &inf,const std::wstring &hardwareId,const EventSink&);
    void setOperationOptions(const Paths&);
    int archiveCommand(const std::wstring&);
    void setInstalledIdLog(const std::wstring&);
private:
    struct Impl;
    std::unique_ptr<Impl> m;
    OriginCore(const OriginCore&) = delete;
    OriginCore& operator=(const OriginCore&) = delete;
};

// Validation is isolated and executable in tests without Windows or a driver.
// It deliberately rejects duplicate/stale/ambiguous/unavailable selections.
struct CheckedSelection { const Device* device; const Candidate* candidate; };
std::vector<CheckedSelection> validatePlan(const Snapshot&, const InstallPlan&);
std::wstring canonicalId(const std::wstring&);
bool safeRelativeInfPath(const std::wstring&);
Result decodeLegacyInfResult(int status,bool reboot);
}
#endif
