#include "../src/activity_log.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>
#include <stdexcept>
int main(int argc,char **argv){QCoreApplication app(argc,argv);int count=0;auto check=[&](bool value,const char *message){if(!value)throw std::runtime_error(message);++count;};try{
    QTemporaryDir temp;const QString path=temp.path()+QStringLiteral("/events.jsonl");
    check(ActivityLog::readRecent(path).isEmpty(),"missing log must be an empty view");
    QFile file(path);file.open(QIODevice::WriteOnly);file.write("malformed\n");
    for(int n=0;n<4;++n){QJsonObject row;row.insert(QStringLiteral("message"),QString::fromUtf8("حدث")+QString::number(n));row.insert(QStringLiteral("win32Error"),n==3?1058:0);file.write(QJsonDocument(row).toJson(QJsonDocument::Compact));file.write("\n");}
    file.write("{\"message\":\"incomplete");file.close();
    const auto all=ActivityLog::readRecent(path);check(all.size()==4,"malformed and incomplete lines skipped");
    check(all.last().value(QStringLiteral("message")).toString()==QString::fromUtf8("حدث3"),"UTF-8 log messages retained");
    check(all.last().value(QStringLiteral("win32Error")).toInt()==1058,"native Windows code retained");
    const auto recent=ActivityLog::readRecent(path,2);check(recent.size()==2 && recent.first().value(QStringLiteral("message")).toString()==QString::fromUtf8("حدث2"),"only newest requested records kept");
    const auto bounded=ActivityLog::readRecent(path,10,100);check(bounded.size()<4 && !bounded.isEmpty(),"byte-tail limit skips partial leading record");
    check(ActivityLog::readRecent(path,0).isEmpty(),"invalid limits refused");
    printf("%d activity-log checks passed; no logs modified.\n",count);return 0;
}catch(const std::exception &e){fprintf(stderr,"Log test after %d: %s\n",count,e.what());return 1;}}
