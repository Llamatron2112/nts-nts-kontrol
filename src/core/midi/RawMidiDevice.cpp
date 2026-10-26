#include "core/midi/RawMidiDevice.h"

#include "core/midi/SysExAssembler.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <sound/asound.h>

namespace korg {

RawMidiDevice::~RawMidiDevice()
{
    close();
}

QList<MidiPort> RawMidiDevice::enumerate()
{
    struct CardInfo {
        int index = -1;
        QString id;
        QString name;
    };

    QList<CardInfo> cards;
    QFile cardsFile(QStringLiteral("/proc/asound/cards"));
    if (cardsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromLocal8Bit(cardsFile.readAll());
        const QStringList lines = text.split(QLatin1Char('\n'));
        const QRegularExpression cardRe(
            QStringLiteral("^\\s*(\\d+)\\s+\\[([^\\]]+)\\]\\s*:\\s*(.*)$"));
        for (const QString &line : lines) {
            const auto match = cardRe.match(line);
            if (!match.hasMatch())
                continue;
            CardInfo card;
            card.index = match.captured(1).toInt();
            card.id = match.captured(2).trimmed();
            QString rest = match.captured(3).trimmed();
            const int dash = rest.indexOf(QStringLiteral(" - "));
            if (dash >= 0)
                rest = rest.mid(dash + 3).trimmed();
            card.name = rest;
            cards.append(card);
        }
    }

    if (cards.isEmpty()) {
        for (int index = 0; index < 32; ++index) {
            QFile idFile(QStringLiteral("/proc/asound/card%1/id").arg(index));
            if (!idFile.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            CardInfo card;
            card.index = index;
            card.id = QString::fromLocal8Bit(idFile.readAll()).trimmed();
            card.name = card.id;
            QFile nameFile(QStringLiteral("/proc/asound/card%1/name").arg(index));
            if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text))
                card.name = QString::fromLocal8Bit(nameFile.readAll()).trimmed();
            cards.append(card);
        }
    }

    QList<MidiPort> ports;
    for (const CardInfo &card : cards) {
        const QString cardDir = QStringLiteral("/proc/asound/card%1").arg(card.index);
        QDir dir(cardDir);
        if (!dir.exists())
            continue;

        const QStringList midiFiles = dir.entryList({QStringLiteral("midi*")},
                                                    QDir::Files, QDir::Name);
        for (const QString &midiFile : midiFiles) {
            const QRegularExpression midiRe(QStringLiteral("^midi(\\d+)$"));
            const auto match = midiRe.match(midiFile);
            if (!match.hasMatch())
                continue;
            const int deviceNumber = match.captured(1).toInt();

            QFile infoFile(dir.filePath(midiFile));
            if (!infoFile.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QStringList lines =
                QString::fromLocal8Bit(infoFile.readAll()).split(QLatin1Char('\n'));

            QString portName;
            QSet<int> subdevices;
            for (const QString &rawLine : lines) {
                const QString line = rawLine.trimmed();
                if (portName.isEmpty() && !line.isEmpty()
                    && !line.startsWith(QStringLiteral("Type:"))
                    && !line.startsWith(QStringLiteral("Output"))
                    && !line.startsWith(QStringLiteral("Input"))) {
                    portName = line;
                }
                const QRegularExpression subRe(
                    QStringLiteral("^(?:Output|Input)\\s+(\\d+)$"));
                const auto subMatch = subRe.match(line);
                if (subMatch.hasMatch())
                    subdevices.insert(subMatch.captured(1).toInt());
            }
            if (subdevices.isEmpty())
                subdevices.insert(0);

            QString displayCard = card.name;
            if (displayCard.isEmpty())
                displayCard = card.id;

            for (int subdevice : subdevices) {
                MidiPort port;
                port.backend = MidiBackendType::AlsaRaw;
                port.card = card.index;
                port.device = deviceNumber;
                port.subdevice = subdevice;
                port.cardId = card.id;
                port.cardName = displayCard;
                port.portName = portName;
                port.devicePath = QStringLiteral("/dev/snd/midiC%1D%2")
                                      .arg(card.index)
                                      .arg(deviceNumber);
                port.controlPath = QStringLiteral("/dev/snd/controlC%1").arg(card.index);
                const QString haystack = (displayCard + QLatin1Char(' ') + card.id
                                          + QLatin1Char(' ') + portName).toLower();
                port.isKorg = haystack.contains(QStringLiteral("nts-1"))
                              || haystack.contains(QStringLiteral("korg"))
                              || haystack.contains(QStringLiteral("logue"));
                ports.append(port);
            }
        }
    }
    return ports;
}

bool RawMidiDevice::open(const MidiPort &port, QString *error)
{
    close();
    if (port.backend != MidiBackendType::AlsaRaw) {
        if (error)
            *error = QStringLiteral("The selected port is not an ALSA raw MIDI port.");
        return false;
    }
    if (port.controlPath.isEmpty() || port.devicePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("Invalid MIDI port description.");
        return false;
    }

    m_controlFd = ::open(port.controlPath.toLocal8Bit().constData(),
                         O_RDWR | O_CLOEXEC);
    if (m_controlFd < 0) {
        if (error) {
            *error = QStringLiteral("Cannot open %1: %2")
                         .arg(port.controlPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    int preferred = port.subdevice;
    if (::ioctl(m_controlFd, SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE, &preferred) < 0) {
        if (error) {
            *error = QStringLiteral("Cannot select MIDI subdevice %1 on %2: %3")
                         .arg(port.subdevice)
                         .arg(port.controlPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        close();
        return false;
    }

    m_midiFd = ::open(port.devicePath.toLocal8Bit().constData(),
                      O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_midiFd < 0) {
        if (error) {
            *error = QStringLiteral("Cannot open %1: %2")
                         .arg(port.devicePath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        close();
        return false;
    }

    configureStream(SNDRV_RAWMIDI_STREAM_INPUT);
    configureStream(SNDRV_RAWMIDI_STREAM_OUTPUT);

    m_port = port;
    m_buffer.clear();
    return true;
}

void RawMidiDevice::close()
{
    if (m_midiFd >= 0) {
        ::close(m_midiFd);
        m_midiFd = -1;
    }
    if (m_controlFd >= 0) {
        ::close(m_controlFd);
        m_controlFd = -1;
    }
    m_buffer.clear();
}

bool RawMidiDevice::configureStream(int stream)
{
    if (m_midiFd < 0)
        return false;

    snd_rawmidi_params params {};
    params.stream = stream;
    params.buffer_size = 1 << 16;
    params.avail_min = 1;
    params.no_active_sensing = 1;
    params.mode = 0;
    return ::ioctl(m_midiFd, SNDRV_RAWMIDI_IOCTL_PARAMS, &params) >= 0;
}

bool RawMidiDevice::writeAll(const char *data, qsizetype size, QString *error)
{
    qsizetype written = 0;
    while (written < size) {
        const ssize_t result = ::write(m_midiFd, data + written, size_t(size - written));
        if (result > 0) {
            written += result;
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd {};
            pfd.fd = m_midiFd;
            pfd.events = POLLOUT;
            const int polled = ::poll(&pfd, 1, 3000);
            if (polled > 0)
                continue;
            if (error)
                *error = QStringLiteral("Timed out writing to the MIDI device.");
            return false;
        }
        if (error) {
            *error = QStringLiteral("MIDI write failed: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
    return true;
}

bool RawMidiDevice::send(const QByteArray &bytes, QString *error)
{
    if (m_midiFd < 0) {
        if (error)
            *error = QStringLiteral("MIDI device is not open.");
        return false;
    }
    return writeAll(bytes.constData(), bytes.size(), error);
}

QByteArray RawMidiDevice::receive(int timeoutMs, QString *error)
{
    if (m_midiFd < 0) {
        if (error)
            *error = QStringLiteral("MIDI device is not open.");
        return {};
    }

    QElapsedTimer timer;
    timer.start();

    while (true) {
        QByteArray frame = takeSysExFrame(&m_buffer);
        if (!frame.isEmpty())
            return frame;

        const qint64 remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            if (error)
                *error = QStringLiteral("Timed out waiting for a MIDI SysEx reply.");
            return {};
        }

        pollfd pfd {};
        pfd.fd = m_midiFd;
        pfd.events = POLLIN;
        const int polled = ::poll(&pfd, 1, int(qMin<qint64>(remaining, 1000)));
        if (polled < 0) {
            if (errno == EINTR)
                continue;
            if (error) {
                *error = QStringLiteral("MIDI poll failed: %1")
                             .arg(QString::fromLocal8Bit(std::strerror(errno)));
            }
            return {};
        }
        if (polled == 0)
            continue;

        char buffer[8192];
        const ssize_t got = ::read(m_midiFd, buffer, sizeof(buffer));
        if (got < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            if (error) {
                *error = QStringLiteral("MIDI read failed: %1")
                             .arg(QString::fromLocal8Bit(std::strerror(errno)));
            }
            return {};
        }
        if (got > 0) {
            m_buffer.append(buffer, int(got));
        }
    }
}

void RawMidiDevice::discardInput()
{
    m_buffer.clear();
    if (m_midiFd < 0)
        return;
    char buffer[4096];
    while (true) {
        const ssize_t got = ::read(m_midiFd, buffer, sizeof(buffer));
        if (got <= 0)
            break;
    }
}

} // namespace korg
