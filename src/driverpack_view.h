#ifndef DRIVERPACK_VIEW_H
#define DRIVERPACK_VIEW_H
#include <QVector>
#include <QtGlobal>
namespace DriverPackView {
// Explicit DriverPack presentation policy; no SDIO score/status filter bitmaps.
inline bool accept(quint32 status,bool eligible,bool local,const QVector<bool> &filters){
 if(filters.size()!=7)return false;
 if(!eligible&&!filters[5])return false;
 if(!local&&!filters[4])return false;
 const quint32 versions[]={0x10,0x20,0x40,0x80};
 for(int i=0;i<4;++i)if((status&versions[i])&&filters[i])return true;
 return false;
}
}
#endif
