#ifndef RPMSGLISTENER_H
#define RPMSGLISTENER_H

#include <QThread>
#include <QString>
#include <atomic>

class RpmsgListener : public QThread {
    Q_OBJECT
public:
    explicit RpmsgListener(const QString &device = "/dev/ttyRPMSG0",
                           QObject *parent = nullptr);
    ~RpmsgListener();

    void stop();
    bool sendCommand(const QString &cmd);

signals:
    void temperatureUpdated(double tempC);
    void humidityUpdated(double humiPct);
    void freezeRequested();
    void captureRequested();
    void alarmReceived(int level);
    void connectionChanged(bool connected);

protected:
    void run() override;

private:
    void parseLine(const QByteArray &line);

    QString m_device;
    int m_fd;
    std::atomic<bool> m_running;
};

#endif // RPMSGLISTENER_H
