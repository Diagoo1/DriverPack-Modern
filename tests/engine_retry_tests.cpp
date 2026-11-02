// SPDX-License-Identifier: GPL-3.0-or-later
// Real EngineBridge and real QThread completion/restart; backend cannot install.
#include "../src/engine_bridge.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
namespace sdiomodern {struct OriginCore::Impl {};OriginCore::~OriginCore(){}}
DeviceScanner::DeviceScanner(QObject *p):QThread(p),m_demo(false){}
void DeviceScanner::setDemo(bool b){m_demo=b;}
void DeviceScanner::run(){}
OriginWorker::OriginWorker(QObject *p):QThread(p),job(Scan),m_stop(false){}
OriginWorker::~OriginWorker(){wait();}
void OriginWorker::stopAfterCurrent(){m_stop.store(true);}
void OriginWorker::resetRequest(){installation=sdiomodern::InstallReport();error.clear();m_stop.store(false);}
void OriginWorker::run(){
    if(!plan.continueOnRestoreFailure){installation.restoreFailed=true;installation.restoreError=1058;}
    else for(size_t i=0;i<plan.selections.size();++i){sdiomodern::Result r;r.deviceId=L"fixture";r.outcome=sdiomodern::Outcome::Success;installation.items.push_back(r);}
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);EngineBridge bridge;
    sdiomodern::InstallPlan plan;plan.createRestorePoint=true;plan.confirmed=true;
    const auto args=app.arguments();const bool cancel=args.contains(QStringLiteral("--cancel")),defer=args.contains(QStringLiteral("--defer"));
    const int selected=args.contains(QStringLiteral("--all"))?24:1;plan.selections.resize(selected);
    int calls=0,observed=0;bool valid=true;
    QObject::connect(&bridge,&EngineBridge::nativeInstallationFinished,&app,[&](const sdiomodern::InstallReport &report){
        ++calls;
        if(calls==1){
            valid&=report.restoreFailed && report.restoreError==1058;
            if(cancel){QTimer::singleShot(0,&app,&QCoreApplication::quit);return;}
            plan.continueOnRestoreFailure=true;
            // Deliberately exercise the most aggressive receiver: resets the
            // worker output inside this very signal, before the next receiver.
            if(defer)QTimer::singleShot(0,&app,[&](){valid&=bridge.requestInstallation(plan);});
            else valid&=bridge.requestInstallation(plan);
            valid&=report.restoreFailed && report.restoreError==1058;
        }else {valid&=report.items.size()==size_t(selected) && !report.restoreFailed;QTimer::singleShot(0,&app,&QCoreApplication::quit);}
    });
    QObject::connect(&bridge,&EngineBridge::nativeInstallationFinished,&app,[&](const sdiomodern::InstallReport &report){
        ++observed;if(observed==1)valid&=report.restoreFailed && report.restoreError==1058;
    });
    QTimer::singleShot(5000,&app,[&](){valid=false;app.quit();});
    if(!bridge.requestInstallation(plan))return 2;app.exec();
    const int expected=cancel?1:2;
    if(!valid||calls!=expected||observed!=expected){fprintf(stderr,"retry lifetime failed calls=%d observed=%d\n",calls,observed);return 1;}
    printf("Thread restart and report lifetime passed selected=%d cancel=%d defer=%d; no Windows driver or restore API.\n",selected,cancel,defer);return 0;
}
