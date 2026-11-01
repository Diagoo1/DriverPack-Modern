#include "automation_plan.h"
#include "origin_worker.h"
#include "offline_paths.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QSet>
#include <stdexcept>

static void rejectPlan(const char *text){throw std::runtime_error(text);}
QJsonObject automationPlan(const DriverCatalog &catalog,bool restore){
    if(!catalog.native() || !catalog.nativeInstallEnabled() || !catalog.nativeSnapshot().generation)rejectPlan("A current installation-capable scan is required.");
    QJsonObject root;root.insert(QStringLiteral("schema"),1);root.insert(QStringLiteral("sourceRevision"),QString::fromStdWString(catalog.nativeSnapshot().sourceRevision));root.insert(QStringLiteral("createRestorePoint"),restore);root.insert(QStringLiteral("allowAlternatives"),true);QJsonArray selections;
    for(const DriverEntry &entry:catalog.entries()){
        if(!entry.checked || entry.chosenCandidate<=0 || entry.chosenCandidate>=entry.candidates.size())continue;
        const DriverCandidate &c=entry.candidates[entry.chosenCandidate];QJsonObject item;item.insert(QStringLiteral("instanceId"),entry.device.instanceId);item.insert(QStringLiteral("package"),c.packageSource);item.insert(QStringLiteral("inf"),c.inf);item.insert(QStringLiteral("version"),c.version);item.insert(QStringLiteral("hardwareId"),c.hardwareId);item.insert(QStringLiteral("originalOrder"),c.originalOrder);item.insert(QStringLiteral("rawScore"),static_cast<double>(c.rawScore));selections.append(item);
    }
    if(selections.isEmpty())rejectPlan("Select drivers before exporting an automation plan.");root.insert(QStringLiteral("selections"),selections);return root;
}
sdiomodern::InstallPlan resolveAutomationPlan(const sdiomodern::Snapshot &snapshot,const QJsonObject &root,bool accepted){
    if(!snapshot.capabilities.localInstall || !snapshot.generation)rejectPlan("Automation requires a current real-machine installation-capable scan.");
    if(snapshot.sourceRevision.empty() || root.value(QStringLiteral("schema")).toInt()!=1 || root.value(QStringLiteral("sourceRevision")).toString()!=QString::fromStdWString(snapshot.sourceRevision))rejectPlan("Automation plan schema or engine revision does not match.");
    const QJsonArray selections=root.value(QStringLiteral("selections")).toArray();if(selections.isEmpty() || selections.size()>1024)rejectPlan("Invalid automation selection count.");
    sdiomodern::InstallPlan plan;plan.generation=snapshot.generation;plan.confirmed=accepted;plan.createRestorePoint=root.value(QStringLiteral("createRestorePoint")).toBool(true);plan.allowAlternatives=root.value(QStringLiteral("allowAlternatives")).toBool(false);QSet<QString> seen;
    for(const QJsonValue &value:selections){
        const QJsonObject item=value.toObject();const QString id=item.value(QStringLiteral("instanceId")).toString();if(id.isEmpty() || seen.contains(id.toUpper()))rejectPlan("Duplicate or missing device identity in automation plan.");seen.insert(id.toUpper());
        const sdiomodern::Candidate *chosen=0;const sdiomodern::Device *device=0;
        for(const sdiomodern::Device &d:snapshot.devices)if(QString::fromStdWString(d.instanceId).compare(id,Qt::CaseInsensitive)==0){if(device)rejectPlan("Ambiguous device identity.");device=&d;}
        if(!device)rejectPlan("An automation device is no longer present.");
        for(const sdiomodern::Candidate &c:device->candidates){
            if(QString::fromStdWString(c.packageName)!=item.value(QStringLiteral("package")).toString() || QString::fromStdWString(c.inf)!=item.value(QStringLiteral("inf")).toString() || QString::fromStdWString(c.version)!=item.value(QStringLiteral("version")).toString() || QString::fromStdWString(c.hardwareId)!=item.value(QStringLiteral("hardwareId")).toString() || c.originalOrder!=item.value(QStringLiteral("originalOrder")).toInt() || static_cast<double>(c.rawScore)!=item.value(QStringLiteral("rawScore")).toDouble(-1))continue;
            if(chosen)rejectPlan("Ambiguous automation driver identity.");chosen=&c;
        }
        if(!chosen || !chosen->valid || !chosen->local || (!chosen->recommended && !plan.allowAlternatives))rejectPlan("An automation driver changed or is not eligible.");
        sdiomodern::Selection selection;selection.deviceInstanceId=device->instanceId;selection.candidateToken=chosen->token;plan.selections.push_back(selection);
    }
    return plan;
}
int runAutomationReport(int argc,char **argv){
    QCoreApplication app(argc,argv);const QStringList args=app.arguments();auto value=[&args](const QString &key){const int index=args.indexOf(key);return index>=0 && index+1<args.size()?args[index+1]:QString();};
    QJsonObject result;result.insert(QStringLiteral("installationRequested"),false);result.insert(QStringLiteral("error"),QString());int code=0;
    try{
        if(value(QStringLiteral("--automation-plan")).isEmpty() || value(QStringLiteral("--automation-report")).isEmpty() || value(QStringLiteral("--drivers")).isEmpty() || value(QStringLiteral("--indexes")).isEmpty())rejectPlan("Explicit plan, report, drivers and indexes paths are required.");
        if(args.contains(QStringLiteral("--target-windows")) || args.contains(QStringLiteral("--target-arch")) || args.contains(QStringLiteral("--load-snapshot")))rejectPlan("Automation targets the current computer only; preview and snapshot flags are not accepted.");
        const bool install=args.contains(QStringLiteral("--accept-installation"));if(install && args.contains(QStringLiteral("--validate-plan")))rejectPlan("Validation and installation modes are mutually exclusive.");if(!install && !args.contains(QStringLiteral("--validate-plan")))rejectPlan("Use --validate-plan or explicitly pass --accept-installation.");
        QFile input(value(QStringLiteral("--automation-plan")));if(!input.open(QIODevice::ReadOnly) || input.size()>1024*1024)rejectPlan("Cannot read the bounded automation plan.");QJsonParseError error;const QJsonDocument document=QJsonDocument::fromJson(input.readAll(),&error);if(error.error!=QJsonParseError::NoError || !document.isObject())rejectPlan("Invalid automation plan JSON.");
        OriginWorker worker;worker.paths.drivers=value(QStringLiteral("--drivers")).toStdWString();worker.paths.indexes=resolveIndexDirectory(value(QStringLiteral("--indexes"))).toStdWString();worker.paths.logDirectory=QDir(QFileInfo(value(QStringLiteral("--automation-report"))).absolutePath()).filePath(QStringLiteral("automation-logs")).toStdWString();worker.resetRequest();worker.job=OriginWorker::Scan;worker.start();worker.wait();if(!worker.error.isEmpty())throw std::runtime_error(worker.error.toStdString());
        worker.plan=resolveAutomationPlan(worker.output,document.object(),install);result.insert(QStringLiteral("validatedSelections"),static_cast<int>(worker.plan.selections.size()));
        if(install){worker.resetRequest();worker.job=OriginWorker::Install;result.insert(QStringLiteral("installationRequested"),true);worker.start();worker.wait();if(!worker.error.isEmpty())throw std::runtime_error(worker.error.toStdString());QJsonArray outcomes;bool failed=false;for(const sdiomodern::Result &outcome:worker.installation.items){QJsonObject item;item.insert(QStringLiteral("instanceId"),QString::fromStdWString(outcome.deviceId));item.insert(QStringLiteral("message"),QString::fromStdWString(outcome.message));item.insert(QStringLiteral("outcome"),static_cast<int>(outcome.outcome));item.insert(QStringLiteral("nativeError"),static_cast<double>(outcome.nativeError));outcomes.append(item);failed=failed || outcome.outcome==sdiomodern::Outcome::Failure;}result.insert(QStringLiteral("results"),outcomes);result.insert(QStringLiteral("rebootRequired"),worker.installation.rebootRequired);if(!worker.installation.error.empty()){result.insert(QStringLiteral("error"),QString::fromStdWString(worker.installation.error));failed=true;}if(worker.installation.cancelled || worker.installation.items.empty())failed=true;if(failed)code=1;}
    }catch(const std::exception &e){result.insert(QStringLiteral("error"),QString::fromUtf8(e.what()));code=1;}
    QSaveFile output(value(QStringLiteral("--automation-report")));const QByteArray bytes=QJsonDocument(result).toJson();if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit())return 3;return code;
}
