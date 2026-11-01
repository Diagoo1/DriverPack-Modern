#ifndef DRIVERPACK_HARDWARE_REPORT_H
#define DRIVERPACK_HARDWARE_REPORT_H
#include <QWidget>
#include <QJsonObject>
#include <QThread>
#include <atomic>
class QTreeWidget;class QPushButton;class QLabel;class QLineEdit;
int runHardwareReport(int argc,char **argv);
QString hardwarePropertyLabel(const QString&,bool);
class HardwareWorker:public QThread {
public:explicit HardwareWorker(QObject *parent):QThread(parent),stop(false){}std::atomic<bool> stop;QJsonObject result;QString error;
protected:void run() Q_DECL_OVERRIDE;
};

#endif
