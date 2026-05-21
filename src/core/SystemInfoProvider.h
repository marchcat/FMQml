#pragma once

#include <QObject>
#include <QTimer>
#include <QDateTime>

class SystemInfoProvider final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY cpuUsageChanged)
    Q_PROPERTY(double ramUsage READ ramUsage NOTIFY ramUsageChanged)
    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString computerName READ computerName CONSTANT)
    Q_PROPERTY(QString cpuArchitecture READ cpuArchitecture CONSTANT)
    Q_PROPERTY(QString uptime READ uptime NOTIFY uptimeChanged)

public:
    explicit SystemInfoProvider(QObject *parent = nullptr);

    double cpuUsage() const { return m_cpuUsage; }
    double ramUsage() const { return m_ramUsage; }
    QString osName() const { return m_osName; }
    QString computerName() const { return m_computerName; }
    QString cpuArchitecture() const { return m_cpuArchitecture; }
    QString uptime() const;

signals:
    void cpuUsageChanged();
    void ramUsageChanged();
    void uptimeChanged();

private slots:
    void updateStats();

private:
    QTimer *m_timer = nullptr;
    double m_cpuUsage = 0.0;
    double m_ramUsage = 0.0;
    QString m_osName;
    QString m_computerName;
    QString m_cpuArchitecture;
    qint64 m_startTime = 0;
};
