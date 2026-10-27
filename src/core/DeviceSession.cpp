#include "core/DeviceSession.h"

#include "core/model/GlobalData.h"
#ifdef HAVE_JACK
#include "core/midi/JackMidiDevice.h"
#endif

#include <QElapsedTimer>
#include <QMap>
#include <QSet>
#include <QThread>

#include <algorithm>

namespace korg {

namespace {

constexpr int kSearchTimeoutMs = 1200;
constexpr int kJackSearchTimeoutMs = 2200;
constexpr int kTransferTimeoutMs = 8000;

quint32 readLe32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return quint32(quint8(data[offset]))
            | (quint32(quint8(data[offset + 1])) << 8)
            | (quint32(quint8(data[offset + 2])) << 16)
            | (quint32(quint8(data[offset + 3])) << 24);
}

void fillDeviceInfo(DeviceInfo *info, const MidiPort &port, const SearchReply &reply)
{
    info->port = port;
    info->channel = reply.channel;
    info->family = reply.family;
    info->member = reply.member;
    info->majorVersion = reply.majorVersion;
    info->minorVersion = reply.minorVersion;
    info->valid = true;
}

bool probeSearch(MidiBackend &backend, SearchReply *reply, QString *error,
                 int timeoutMs = kSearchTimeoutMs)
{
    backend.discardInput();
    if (!backend.send(searchRequest(0x55), error))
        return false;

    QElapsedTimer timer;
    timer.start();
    QString lastError;
    while (timer.elapsed() < timeoutMs) {
        const int remaining = int(qMax<qint64>(1, timeoutMs - timer.elapsed()));
        const QByteArray frame = backend.receive(remaining, &lastError);
        if (frame.isEmpty())
            break;
        const ParsedSysex message = parseSysex(frame);
        if (message.kind != ParsedSysex::Kind::SearchReply)
            continue;
        const SearchReply parsed = parseSearchReply(message);
        if (!parsed.valid)
            continue;
        if (parsed.family != ((quint16(familyIdMsb) << 8) | familyIdLsb))
            continue;
        if (reply)
            *reply = parsed;
        return true;
    }

    if (error)
        *error = lastError.isEmpty() ? QStringLiteral("The device did not respond.")
                                     : lastError;
    return false;
}

} // namespace

QString DeviceInfo::displayName() const
{
    return QStringLiteral("%1 — %2").arg(port.displayName(), port.backendName());
}

QString DeviceInfo::versionString() const
{
    return QStringLiteral("%1.%2").arg(majorVersion).arg(minorVersion);
}

DeviceSession::~DeviceSession()
{
    close();
}

bool DeviceSession::pipeWireAvailable()
{
    return midiPipeWireAvailable();
}

QList<DeviceInfo> DeviceSession::scan(MidiBackendPreference preference, QString *error)
{
    QList<DeviceInfo> found;
    QSet<QString> successfulDevices;

    if (preference != MidiBackendPreference::AlsaRaw) {
#ifdef HAVE_JACK
        const QList<MidiPort> candidates = JackMidiDevice::enumerate();
        for (const MidiPort &port : candidates) {
            if (successfulDevices.contains(port.jackDeviceKey))
                continue;
            JackMidiDevice midi;
            QString openError;
            if (!midi.open(port, &openError))
                continue;
            // Give the PipeWire/JACK bridge a moment to finish connecting the
            // MIDI ports before sending the first SysEx request.
            QThread::msleep(120);
            SearchReply reply;
            QString probeError;
            if (probeSearch(midi, &reply, &probeError, kJackSearchTimeoutMs)) {
                DeviceInfo info;
                fillDeviceInfo(&info, port, reply);
                found.append(info);
                successfulDevices.insert(port.jackDeviceKey);
            }
            midi.close();
        }
#else
        if (preference == MidiBackendPreference::PipeWire) {
            if (error)
                *error = QStringLiteral("The PipeWire MIDI backend was not built. "
                                        "Install pipewire-jack-audio-connection-kit-devel "
                                        "and rebuild.");
            return {};
        }
#endif
        if (!found.isEmpty()) {
            if (error)
                error->clear();
            return found;
        }
        if (preference == MidiBackendPreference::PipeWire) {
            if (error)
                *error = QStringLiteral("The NTS-1 was not found on PipeWire's Midi-Bridge. "
                                        "Check the USB connection and that PipeWire is running.");
            return {};
        }
    }

    QList<MidiPort> ports = RawMidiDevice::enumerate();
    std::sort(ports.begin(), ports.end(), [](const MidiPort &a, const MidiPort &b) {
        if (a.card != b.card)
            return a.card < b.card;
        if (a.device != b.device)
            return a.device < b.device;
        return a.subdevice > b.subdevice;
    });

    for (const MidiPort &port : ports) {
        if (!port.isKorg)
            continue;
        const QString key = QStringLiteral("%1:%2").arg(port.card).arg(port.device);
        if (successfulDevices.contains(key))
            continue;

        for (int attempt = 0; attempt < 2 && !successfulDevices.contains(key); ++attempt) {
            RawMidiDevice midi;
            QString openError;
            if (!midi.open(port, &openError))
                break;

            QThread::msleep(30);
            SearchReply reply;
            QString probeError;
            if (probeSearch(midi, &reply, &probeError, kSearchTimeoutMs)) {
                DeviceInfo info;
                fillDeviceInfo(&info, port, reply);
                found.append(info);
                successfulDevices.insert(key);
            }
            midi.close();
            if (!successfulDevices.contains(key))
                QThread::msleep(60);
        }
    }

    if (found.isEmpty()) {
        if (error) {
            if (preference == MidiBackendPreference::AlsaRaw) {
                *error = QStringLiteral("No KORG NTS-1 mkII device was found over raw ALSA MIDI. "
                                        "If PipeWire is running it may own the MIDI device; "
                                        "use the PipeWire backend.");
            } else {
                *error = QStringLiteral("No KORG NTS-1 mkII device was found over PipeWire or "
                                        "raw ALSA MIDI. Check the USB connection and the MIDI "
                                        "bridge.");
            }
        }
    } else if (error) {
        error->clear();
    }
    return found;
}

bool DeviceSession::open(const DeviceInfo &info, QString *error)
{
    close();
    if (!info.valid) {
        if (error)
            *error = QStringLiteral("Invalid device information.");
        return false;
    }

    if (info.port.backend == MidiBackendType::PipeWireJack) {
#ifdef HAVE_JACK
        m_midi = std::make_unique<JackMidiDevice>();
#else
        if (error)
            *error = QStringLiteral("The PipeWire MIDI backend was not built.");
        return false;
#endif
    } else {
        m_midi = std::make_unique<RawMidiDevice>();
    }

    if (!m_midi->open(info.port, error)) {
        m_midi.reset();
        return false;
    }
    m_info = info;
    m_open = true;
    m_midi->discardInput();
    QThread::msleep(150);
    m_midi->discardInput();
    return true;
}

void DeviceSession::close()
{
    if (m_midi) {
        m_midi->close();
        m_midi.reset();
    }
    m_open = false;
}

bool DeviceSession::identify(QString *error)
{
    if (!m_open || !m_midi) {
        if (error)
            *error = QStringLiteral("The MIDI device is not open.");
        return false;
    }

    SearchReply reply;
    if (!probeSearch(*m_midi, &reply, error))
        return false;
    m_info.channel = reply.channel;
    m_info.family = reply.family;
    m_info.member = reply.member;
    m_info.majorVersion = reply.majorVersion;
    m_info.minorVersion = reply.minorVersion;
    m_info.valid = true;
    return true;
}

bool DeviceSession::receiveLogue(int expectedId, ParsedSysex *reply, QString *error,
                                 int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    QString lastError;

    while (timer.elapsed() < timeoutMs) {
        const int remaining = int(qMax<qint64>(1, timeoutMs - timer.elapsed()));
        const QByteArray frame = m_midi->receive(remaining, &lastError);
        if (frame.isEmpty())
            break;
        const ParsedSysex message = parseSysex(frame);
        if (!message.isLogue())
            continue;
        if (message.isStatus() && message.id != expectedId) {
            if (error)
                *error = statusDescription(message.id);
            return false;
        }
        if (message.id == expectedId) {
            if (reply)
                *reply = message;
            return true;
        }
    }

    if (error)
        *error = lastError.isEmpty()
                      ? QStringLiteral("Timed out waiting for the device.")
                      : lastError;
    return false;
}

bool DeviceSession::transact(const QByteArray &request, int expectedId,
                             ParsedSysex *reply, QString *error, int timeoutMs)
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("The MIDI device is not open.");
        return false;
    }
    if (request.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not build the MIDI request.");
        return false;
    }

    m_midi->discardInput();
    QString ioError;
    if (!m_midi->send(request, &ioError)) {
        if (error)
            *error = ioError;
        return false;
    }
    return receiveLogue(expectedId, reply, error, timeoutMs);
}

bool DeviceSession::sendAndAwaitAck(const QByteArray &message, QString *error,
                                    int timeoutMs)
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("The MIDI device is not open.");
        return false;
    }
    if (message.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not build the MIDI message.");
        return false;
    }

    m_midi->discardInput();
    QString ioError;
    if (!m_midi->send(message, &ioError)) {
        if (error)
            *error = ioError;
        return false;
    }
    return receiveLogue(0x23, nullptr, error, timeoutMs);
}

bool DeviceSession::sendNoAck(const QByteArray &message, QString *error, int timeoutMs)
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("The MIDI device is not open.");
        return false;
    }
    if (message.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not build the MIDI message.");
        return false;
    }

    m_midi->discardInput();
    QString ioError;
    if (!m_midi->send(message, &ioError)) {
        if (error)
            *error = ioError;
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const QByteArray frame = m_midi->receive(int(timeoutMs - timer.elapsed()), &ioError);
        if (frame.isEmpty())
            break;
        const ParsedSysex reply = parseSysex(frame);
        if (!reply.isLogue())
            continue;
        if (reply.id == 0x23)
            return true;
        if (reply.isStatus()) {
            if (error)
                *error = statusDescription(reply.id);
            return false;
        }
    }
    return true;
}

bool DeviceSession::getApiVersion(quint8 *platform, int *major, int *minor, int *patch,
                                  QString *error)
{
    ParsedSysex reply;
    if (!transact(userApiVersionRequest(quint8(m_info.channel)), 0x47, &reply, error))
        return false;
    if (reply.payload.size() < 4) {
        if (error)
            *error = QStringLiteral("Invalid API version reply.");
        return false;
    }
    if (platform)
        *platform = quint8(reply.payload[0]);
    if (major)
        *major = quint8(reply.payload[1]);
    if (minor)
        *minor = quint8(reply.payload[2]);
    if (patch)
        *patch = quint8(reply.payload[3]);
    return true;
}

bool DeviceSession::receiveProgram(Program *program, QString *error)
{
    ParsedSysex reply;
    if (!transact(currentProgramRequest(quint8(m_info.channel)), 0x40, &reply, error))
        return false;
    const QByteArray bytes = decode7bit(reply.payload);
    if (!Program::looksValid(bytes)) {
        if (error)
            *error = QStringLiteral("The device returned an invalid program.");
        return false;
    }
    if (program)
        *program = Program::fromBytes(bytes);
    return true;
}

bool DeviceSession::sendProgram(const Program &program, QString *error)
{
    if (!program.isValid()) {
        if (error)
            *error = QStringLiteral("The program data is not valid.");
        return false;
    }
    const QByteArray message = currentProgramDump(quint8(m_info.channel), program.bytes());
    if (!sendNoAck(message, error))
        return false;

    // The NTS-1 mkII does not acknowledge program/global writes. Verify the
    // write by reading the data back, allowing the device a moment to finish.
    for (int attempt = 0; attempt < 4; ++attempt) {
        QThread::msleep(80);
        Program check;
        QString readError;
        if (receiveProgram(&check, &readError) && check.bytes() == program.bytes())
            return true;
    }
    if (error)
        *error = QStringLiteral("The device did not store the program.");
    return false;
}

bool DeviceSession::receiveGlobal(QByteArray *global, QString *error)
{
    ParsedSysex reply;
    if (!transact(globalRequest(quint8(m_info.channel)), 0x51, &reply, error))
        return false;
    const QByteArray bytes = decode7bit(reply.payload);
    if (!GlobalData::looksLikeDeviceData(bytes)) {
        if (error)
            *error = QStringLiteral("The device returned invalid global data.");
        return false;
    }
    if (global)
        *global = bytes;
    return true;
}

bool DeviceSession::sendGlobal(const QByteArray &global, QString *error)
{
    if (!GlobalData::looksLikeDeviceData(global)) {
        if (error)
            *error = QStringLiteral("The global data is not valid.");
        return false;
    }

    const GlobalData data = GlobalData::fromDeviceBytes(global);
    const int oldChannel = m_info.channel;
    const int newChannel = data.midiChannel();
    const QByteArray message = globalDump(quint8(oldChannel), global);
    if (!sendNoAck(message, error))
        return false;

    // MIDI Route = MIDI only turns off the USB MIDI port this session is
    // using, so the device cannot acknowledge or read back over USB. The
    // write itself is still valid; treat it as fire-and-forget.
    if (data.usbMidiDisabled()) {
        m_info.channel = newChannel;
        QThread::msleep(200);
        return true;
    }

    if (newChannel != oldChannel) {
        // The device switches channels as soon as it stores the block.
        m_info.channel = newChannel;
        QThread::msleep(120);
    }

    for (int attempt = 0; attempt < 4; ++attempt) {
        QThread::msleep(80);
        QByteArray check;
        QString readError;
        if (receiveGlobal(&check, &readError) && check == global)
            return true;
    }

    // The write may not have applied. Re-discover the channel so a failed
    // verification does not leave the session on a stale channel.
    if (newChannel != oldChannel) {
        QString ignored;
        identify(&ignored);
    }
    if (error)
        *error = QStringLiteral("The device did not store the global data.");
    return false;
}

bool DeviceSession::getSlotStatus(Module module, int slot, SlotInfo *info, QString *error)
{
    if (slot < 0 || slot >= slotCount(module)) {
        if (error)
            *error = QStringLiteral("Invalid user slot.");
        return false;
    }

    ParsedSysex reply;
    if (!transact(userSlotStatusRequest(quint8(m_info.channel), module, slot),
                  0x49, &reply, error))
        return false;

    SlotInfo result;
    if (reply.payload.size() < 2) {
        if (error)
            *error = QStringLiteral("Invalid user slot status reply.");
        return false;
    }
    if (reply.payload.size() == 2) {
        result.present = false;
        result.valid = true;
        if (info)
            *info = result;
        return true;
    }

    QByteArray encoded = reply.payload.mid(3);
    if (encoded.isEmpty())
        encoded = reply.payload.mid(2);
    const QByteArray decoded = decode7bit(encoded);
    const UserUnit unit = UserUnit::fromBytes(decoded);
    result.present = true;
    result.valid = unit.isValid();
    result.header = unit.header();
    if (!result.valid)
        result.note = QStringLiteral("Unrecognized unit header.");
    if (info)
        *info = result;
    return true;
}

bool DeviceSession::downloadUnit(Module module, int slot, QByteArray *unit,
                                 QString *error,
                                 const std::function<void(int, int)> &progress)
{
    if (!m_open) {
        if (error)
            *error = QStringLiteral("The MIDI device is not open.");
        return false;
    }
    if (slot < 0 || slot >= slotCount(module)) {
        if (error)
            *error = QStringLiteral("Invalid user slot.");
        return false;
    }

    m_midi->discardInput();
    QString ioError;
    if (!m_midi->send(userSlotDataRequest(quint8(m_info.channel), module, slot), &ioError)) {
        if (error)
            *error = ioError;
        return false;
    }

    QMap<int, QByteArray> packets;
    int sequenceMax = -1;
    QElapsedTimer timer;
    timer.start();
    const int totalTimeout = kTransferTimeoutMs * 4;

    while (timer.elapsed() < totalTimeout) {
        if (sequenceMax >= 0 && packets.size() == sequenceMax + 1)
            break;
        const QByteArray frame = m_midi->receive(kTransferTimeoutMs, &ioError);
        if (frame.isEmpty()) {
            if (error)
                *error = ioError.isEmpty() ? QStringLiteral("Timed out receiving user data.")
                                           : ioError;
            return false;
        }
        const ParsedSysex message = parseSysex(frame);
        if (!message.isLogue())
            continue;
        if (message.isStatus()) {
            if (error)
                *error = statusDescription(message.id);
            return false;
        }
        if (message.id != 0x4a)
            continue;
        if (message.payload.size() < 4) {
            if (error)
                *error = QStringLiteral("Invalid user data packet.");
            return false;
        }
        const int packetModule = quint8(message.payload[0]);
        const int packetSlot = quint8(message.payload[1]);
        if (packetModule != int(module) || packetSlot != slot) {
            if (error)
                *error = QStringLiteral("Received user data for the wrong slot.");
            return false;
        }
        const int sequence = quint8(message.payload[2]);
        const int maxSequence = quint8(message.payload[3]);
        sequenceMax = maxSequence;
        packets[sequence] = decode7bit(message.payload.mid(4));
        if (progress)
            progress(packets.size(), maxSequence + 1);
    }

    if (sequenceMax < 0 || packets.size() != sequenceMax + 1) {
        if (error)
            *error = QStringLiteral("Incomplete user data transfer.");
        return false;
    }

    QByteArray host;
    for (int i = 0; i <= sequenceMax; ++i)
        host.append(packets.value(i));

    if (host.size() < 8) {
        if (error)
            *error = QStringLiteral("The device returned an empty user slot.");
        return false;
    }

    const quint32 length = readLe32(host, 0);
    const quint32 expectedCrc = readLe32(host, 4);
    if (length > quint32(host.size() - 8)) {
        if (error)
            *error = QStringLiteral("The user unit length is invalid.");
        return false;
    }
    const QByteArray elf = host.mid(8, int(length));
    if (expectedCrc != 0 && expectedCrc != crc32(elf)) {
        if (error)
            *error = QStringLiteral("The user unit CRC check failed.");
        return false;
    }
    if (unit)
        *unit = elf;
    return true;
}

bool DeviceSession::uploadUnit(Module module, int slot, const QByteArray &unit,
                               QString *error,
                               const std::function<void(int, int)> &progress)
{
    if (unit.isEmpty()) {
        if (error)
            *error = QStringLiteral("The user unit is empty.");
        return false;
    }
    if (slot < 0 || slot >= slotCount(module)) {
        if (error)
            *error = QStringLiteral("Invalid user slot.");
        return false;
    }

    const QList<QByteArray> messages =
        userSlotDataMessages(quint8(m_info.channel), module, slot, unit);
    if (messages.isEmpty()) {
        if (error)
            *error = QStringLiteral("The user unit is too large or invalid.");
        return false;
    }

    for (int i = 0; i < messages.size(); ++i) {
        if (!sendAndAwaitAck(messages.at(i), error, kTransferTimeoutMs))
            return false;
        if (progress)
            progress(i + 1, messages.size());
        QThread::msleep(15);
    }
    return true;
}

bool DeviceSession::clearSlot(Module module, int slot, QString *error)
{
    if (slot < 0 || slot >= slotCount(module)) {
        if (error)
            *error = QStringLiteral("Invalid user slot.");
        return false;
    }
    return sendAndAwaitAck(clearUserSlot(quint8(m_info.channel), module, slot), error);
}

bool DeviceSession::swapSlots(Module module, int slotA, int slotB, QString *error)
{
    if (slotA < 0 || slotB < 0 || slotA >= slotCount(module) || slotB >= slotCount(module)) {
        if (error)
            *error = QStringLiteral("Invalid user slot.");
        return false;
    }
    return sendAndAwaitAck(swapUserSlots(quint8(m_info.channel), module, slotA, slotB),
                           error);
}

} // namespace korg
