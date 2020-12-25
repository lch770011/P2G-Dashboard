#include <misc/types.h>
#include <misc/constants.h>
#include <logic/radar/radar.h>
#include <gui/dashboard/dashboard.h>
#include <gui/status/status.h>
#include <gui/settings/settings.h>
#include <gui/chart/timedata/timedatachart.h>
#include <gui/chart/rangedata/rangedatachart.h>
#include <gui/chart/targetdata/targetdatachart.h>
#ifdef __linux__
    #include "sigwatch.h"
#endif

#include <QApplication>
#include <QThread>
#include <QDebug>
#include <QTime>

void messageHandler(QtMsgType type, QMessageLogContext const &, QString const & msg)
{
    switch (type)
    {
        case QtDebugMsg:
            fprintf(stdout, "%s [DEBUG]: %s\n", QTime::currentTime().toString().toStdString().c_str(), msg.toStdString().c_str());
            fflush(stdout);
            break;
        case QtInfoMsg:
            fprintf(stdout, "%s [INFO]: %s\n", QTime::currentTime().toString().toStdString().c_str(), msg.toStdString().c_str());
            fflush(stdout);
            break;
        case QtWarningMsg:
            fprintf(stderr, "%s [WARNING]: %s\n", QTime::currentTime().toString().toStdString().c_str(), msg.toStdString().c_str());
            break;
        case QtCriticalMsg:
            fprintf(stderr, "%s [CRITICAL]: %s\n", QTime::currentTime().toString().toStdString().c_str(), msg.toStdString().c_str());
            break;
        case QtFatalMsg:
            fprintf(stderr, "%s [FATAL]: %s\n", QTime::currentTime().toString().toStdString().c_str(), msg.toStdString().c_str());
            break;
    }
}

bool tryConnect(Radar & r)
{
    auto attempts = 0;
    while (attempts <  STARTUP_CONNECTION_ATTEMPS)
    {
        attempts++;
        qInfo() << "Connection attempt" << "(" << attempts << "/" << STARTUP_CONNECTION_ATTEMPS << ")";
        if (r.connect())
            break;
        QThread::msleep(STARTUP_CONNECTION_PAUSE_TIME);
    }

    if (attempts == STARTUP_CONNECTION_ATTEMPS)
    {
        qWarning() << "Aborted: Failed to connect to radar sensor. ";
        return false;
    }
    return true;
}

bool tryAddingEndpoints(Radar & r)
{
    qInfo() << "Trying to add base endpoint...";
    if (!r.addEndpoint(EndpointType_t::Base))
    {
        qCritical() << "Error: Failed to add base endpoint. ";
        return false;
    }
    qInfo() << "Successfully added base endpoint.";

    qInfo() << "Trying to add target detection endpoint...";
    if (!r.addEndpoint(EndpointType_t::TargetDetection))
    {
        qCritical() << "Error: Failed to add target detection endpoint. ";
        return false;
    }
    qInfo() << "Successfully added target detection endpoint.";

    return true;
}

bool trySettingUpFrameTrigger(Radar & r)
{
    qInfo() << "Trying to diable automatic frame trigger for base endpoint...";
    if (!r.setAutomaticFrameTrigger(false, EndpointType_t::Base, 0))
    {
        qCritical() << "Error: Failed to disable frame trigger for base endpoint. ";
        return false;
    }
    qInfo() << "Successfully disabled frame trigger for base endpoint.";

    return true;
}


int main(int argc, char *argv[])
{
    // Register the message handler within the application
    qInstallMessageHandler(messageHandler);
    QApplication a(argc, argv);

    // Signal watch for linux to catch all kinds of termination, so the radar can get into a defined state
#ifdef __linux__
    UnixSignalWatcher sigwatch;
    sigwatch.watchForSignal(SIGINT);
    sigwatch.watchForSignal(SIGTERM);
    sigwatch.watchForSignal(SIGQUIT);
    sigwatch.watchForSignal(SIGHUP);
    QObject::connect(&sigwatch, SIGNAL(unixSignal(int)), &a, SLOT(quit()));
#endif

    // Initiate all variables
    Radar r;
    Dashboard d;
    Status s;
    Settings settings;
    TimeDataChart timedata;
    RangeDataChart rangedata;
    TargetDataChart targetdata;

    // Setup the Mainwindow
    d.setStatus(&s);
    d.setSettings(&settings);
    d.setChart(&timedata, ChartType_t::TimeData);
    d.setChart(&rangedata, ChartType_t::RangeData);
    d.setChart(&targetdata, ChartType_t::TargetData);

    QObject::connect(&settings, &Settings::chartThemeChanged, &timedata, &TimeDataChart::setChartTheme);
    QObject::connect(&settings, &Settings::chartThemeChanged, &rangedata, &RangeDataChart::setChartTheme);
    QObject::connect(&settings, &Settings::chartThemeChanged, &targetdata, &TargetDataChart::setChartTheme);

    // Some connection for the statusbar
    QObject::connect(&r, &Radar::connectionChanged, &s, &Status::updateConnection);
    QObject::connect(&r, &Radar::firmwareInformationChanged, &s, &Status::updateFirmwareInformation);
    QObject::connect(&r, &Radar::temperatureChanged, &s, &Status::updateTemperature);
    QObject::connect(&r, &Radar::serialPortChanged, &s, &Status::updateSerialPort);
    QObject::connect(&s, &Status::changed, &d, &Dashboard::updateStatus);

    // Some connections for the plots
    qRegisterMetaType<Targets_t>("Targets_t");
    qRegisterMetaType<DataPoints_t>("DataPoints_t");
    QObject::connect(&r, &Radar::timeDataChanged, &timedata, &TimeDataChart::update);
    QObject::connect(&r, &Radar::rangeDataChanged, &rangedata, &RangeDataChart::update);
    QObject::connect(&r, &Radar::targetDataChanged, &targetdata, &TargetDataChart::update);

    // Try to setup the radar sensor
    if (!tryConnect(r))
        return ERROR_STARTUP_CONNECTION_FAILED;
    if (!tryAddingEndpoints(r))
        return ERROR_STARTUP_ADDING_ENDPOINTS_FAILED;
    if (!trySettingUpFrameTrigger(r))
        return ERROR_STARTUP_FRAMETRIGGER_SETUP_FAILED;

    // Move the radar object into another thread, so the main thread with the gui won't block
    QThread* t = new QThread();
    r.moveToThread(t);

    // Some more connections
    QObject::connect(t, &QThread::started, &r, &Radar::doMeasurement);
#ifdef _WIN32
    QObject::connect(&d, &Dashboard::closed, &r, &Radar::disconnect, Qt::DirectConnection);
#endif

    // Start the thread
    t->start();

    // Main thread execution continues...
    d.show();

#ifdef __linux__
    auto ret = a.exec();
    r.disconnect();
    return ret;
#elif _WIN32
    return a.exec();
#endif
}
