#include "comm/rpmsglistener.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>

RpmsgListener::RpmsgListener(const QString &device, QObject *parent)
    : QThread(parent), m_device(device), m_fd(-1), m_running(false)
{
}

RpmsgListener::~RpmsgListener()
{
    stop();
    wait();
}

void RpmsgListener::stop()
{
    m_running = false;
}

bool RpmsgListener::sendCommand(const QString &cmd)
{
    if (m_fd < 0) return false;
    QByteArray data = cmd.toUtf8();
    if (!data.endsWith('\n')) data.append('\n');

    // 非阻塞写入：临时设置 O_NONBLOCK，写完恢复
    int flags = ::fcntl(m_fd, F_GETFL, 0);
    ::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
    ssize_t n = ::write(m_fd, data.constData(), data.size());
    ::fcntl(m_fd, F_SETFL, flags);
    return n == data.size();
}

void RpmsgListener::run()
{
    m_fd = ::open(m_device.toUtf8().constData(), O_RDWR | O_NOCTTY);
    if (m_fd < 0) {
        qWarning() << "[RPMsg] Cannot open" << m_device << ":" << strerror(errno);
        emit connectionChanged(false);
        return;
    }

    qDebug() << "[RPMsg] Opened" << m_device;
    emit connectionChanged(true);
    m_running = true;

    QByteArray lineBuf;
    char buf[256];

    while (m_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_fd, &fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout

        int ret = ::select(m_fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            qWarning() << "[RPMsg] select error:" << strerror(errno);
            break;
        }
        if (ret == 0) continue; // timeout

        ssize_t n = ::read(m_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            qDebug() << "[RPMsg] read" << n << "bytes";
        }
        if (n <= 0) {
            if (n == 0) continue; // EOF, retry
            if (errno == EINTR || errno == EAGAIN) continue;
            qWarning() << "[RPMsg] read error:" << strerror(errno);
            break;
        }

        // 逐行解析
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                parseLine(lineBuf.trimmed());
                lineBuf.clear();
            } else {
                lineBuf.append(buf[i]);
            }
        }
    }

    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    emit connectionChanged(false);
    qDebug() << "[RPMsg] Closed";
}

void RpmsgListener::parseLine(const QByteArray &line)
{
    if (line.isEmpty()) return;

    qDebug() << "[RPMsg RX]" << line;

    char type = line.at(0);
    QByteArray valueStr = line.mid(1);

    switch (type) {
    case 'T': {
        bool ok;
        int raw = valueStr.toInt(&ok);
        if (ok) emit temperatureUpdated(raw / 10.0);
        break;
    }
    case 'H': {
        bool ok;
        int raw = valueStr.toInt(&ok);
        if (ok) emit humidityUpdated(raw / 10.0);
        break;
    }
    case 'K': {
        int key = valueStr.toInt();
        if (key == 1) emit freezeRequested();
        else if (key == 2) emit captureRequested();
        break;
    }
    case 'A': {
        int level = valueStr.toInt();
        emit alarmReceived(level);
        break;
    }
    default:
        // printf 调试信息等，忽略
        break;
    }
}
