#include "drop_target.h"
#include "offline_paths.h"
#include <QFileInfo>
DropTarget::Kind DropTarget::classify(const QList<QUrl> &urls,QString *path){
 if(urls.size()!=1||!urls.first().isLocalFile())return Rejected;
 const QFileInfo file(urls.first().toLocalFile());if(!file.exists()||file.isSymLink())return Rejected;
 if(file.isFile()&&file.fileName().compare(QStringLiteral("db.sqlite"),Qt::CaseInsensitive)!=0)return Rejected;
 if(!DriverPackLibrary::locate(file.absoluteFilePath()).valid())return Rejected;
 if(path)*path=file.canonicalFilePath();return file.isDir()?DriverFolder:DatabaseFile;
}
