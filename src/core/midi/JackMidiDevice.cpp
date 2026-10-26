#include "core/midi/JackMidiDevice.h"

#include "core/midi/SysExAssembler.h"

#include <QElapsedTimer>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <chrono>

namespace korg {

namespace {

struct JackPair {
    QString base;
    QString variant;
    QString playback;
    QString capture;
};

QString portName(jack_port_t *port)
{
    if (!port)
        return {};
    return QString::fromUtf8(jack_port_name(port));
}

} // namespace

JackMidiDevice::~JackMidiDevice()
{
    close();
}

QList<MidiPort> JackMidiDevice::enumerate()
{
    jack_status_t status;
    jack_client_t *client = jack_client_open("nts-nts-kontrol-enumerate",
                                             JackNoStartServer, &status);
    if (!client)
        return {};
    if (jack_activate(client) != 0) {
        jack_client_close(client);
        return {};
    }

    QMap<QString, JackPair> pairs;
    const char **ports = jack_get_ports(client, nullptr, JACK_DEFAULT_MIDI_TYPE, 0);
    if (ports) {
        const QRegularExpression re(
            QStringLiteral("^(.*) \\((playback|capture)\\)(-\\d+)?$"));
        for (const char **entry = ports; *entry; ++entry) {
            const QString name = QString::fromUtf8(*entry);
            if (!name.contains(QStringLiteral("NTS-1"), Qt::CaseInsensitive))
                continue;
            const auto match = re.match(name);
            if (!match.hasMatch())
                continue;
            const QString base = match.captured(1);
            const QString role = match.captured(2);
            const QString suffix = match.captured(3);
            const QString variantKey = suffix.isEmpty() ? QStringLiteral("plain")
                                                        : QStringLiteral("suffixed");
            const QString key = base + QLatin1Char('|') + variantKey;
            JackPair &pair = pairs[key];
            pair.base = base;
            pair.variant = suffix.isEmpty() ? QString() : suffix;
            if (role == QStringLiteral("playback"))
                pair.playback = name;
            else
                pair.capture = name;
        }
        jack_free(ports);
    }

    jack_deactivate(client);
    jack_client_close(client);

    QList<MidiPort> result;
    for (const JackPair &pair : pairs) {
        if (pair.playback.isEmpty() || pair.capture.isEmpty())
            continue;
        MidiPort port;
        port.backend = MidiBackendType::PipeWireJack;
        port.isKorg = true;
        port.cardName = pair.base.section(QLatin1Char(':'), 1, 1).trimmed();
        if (port.cardName.isEmpty()
            || !port.cardName.contains(QStringLiteral("NTS-1"), Qt::CaseInsensitive))
            port.cardName = pair.base;
        port.portName = QStringLiteral("PipeWire MIDI");
        port.jackPlayback = pair.playback;
        port.jackCapture = pair.capture;
        port.jackDeviceKey = pair.base;
        port.jackVariant = pair.variant;
        result.append(port);
    }

    std::sort(result.begin(), result.end(), [](const MidiPort &a, const MidiPort &b) {
        const bool aVariant = !a.jackVariant.isEmpty();
        const bool bVariant = !b.jackVariant.isEmpty();
        if (aVariant != bVariant)
            return aVariant;
        return a.jackDeviceKey < b.jackDeviceKey;
    });
    return result;
}

bool JackMidiDevice::open(const MidiPort &port, QString *error)
{
    close();
    if (port.backend != MidiBackendType::PipeWireJack) {
        if (error)
            *error = QStringLiteral("The selected port is not a PipeWire MIDI port.");
        return false;
    }
    if (port.jackPlayback.isEmpty() || port.jackCapture.isEmpty()) {
        if (error)
            *error = QStringLiteral("The PipeWire MIDI port pair is incomplete.");
        return false;
    }

    jack_status_t status;
    m_client = jack_client_open("nts-nts-kontrol", JackNoStartServer, &status);
    if (!m_client) {
        if (error) {
            *error = QStringLiteral("Could not connect to PipeWire's JACK MIDI server "
                                    "(status 0x%1). Is PipeWire running?")
                         .arg(quint32(status), 0, 16);
        }
        return false;
    }

    m_in = jack_port_register(m_client, "midi_in", JACK_DEFAULT_MIDI_TYPE,
                              JackPortIsInput, 0);
    m_out = jack_port_register(m_client, "midi_out", JACK_DEFAULT_MIDI_TYPE,
                               JackPortIsOutput, 0);
    if (!m_in || !m_out) {
        if (error)
            *error = QStringLiteral("Could not register PipeWire MIDI ports.");
        close();
        return false;
    }

    jack_set_process_callback(m_client, &JackMidiDevice::processCallback, this);
    if (jack_activate(m_client) != 0) {
        if (error)
            *error = QStringLiteral("Could not activate the PipeWire MIDI client.");
        close();
        return false;
    }

    m_open.store(true);
    const QByteArray outName = portName(m_out).toUtf8();
    const QByteArray inName = portName(m_in).toUtf8();
    const QByteArray playback = port.jackPlayback.toUtf8();
    const QByteArray capture = port.jackCapture.toUtf8();

    const int outResult = jack_connect(m_client, outName.constData(), playback.constData());
    if (outResult != 0 && outResult != EEXIST) {
        if (error) {
            *error = QStringLiteral("Could not connect to the NTS-1 PipeWire playback port "
                                    "(error %1).")
                         .arg(outResult);
        }
        close();
        return false;
    }
    const int inResult = jack_connect(m_client, capture.constData(), inName.constData());
    if (inResult != 0 && inResult != EEXIST) {
        if (error) {
            *error = QStringLiteral("Could not connect to the NTS-1 PipeWire capture port "
                                    "(error %1).")
                         .arg(inResult);
        }
        close();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tx.clear();
        m_buffer.clear();
    }
    m_port = port;
    m_open.store(true);
    return true;
}

void JackMidiDevice::close()
{
    const bool wasOpen = m_open.exchange(false);
    m_cv.notify_all();

    if (m_client) {
        jack_deactivate(m_client);
        if (m_in)
            jack_port_unregister(m_client, m_in);
        if (m_out)
            jack_port_unregister(m_client, m_out);
        jack_client_close(m_client);
    }
    m_client = nullptr;
    m_in = nullptr;
    m_out = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tx.clear();
        m_buffer.clear();
    }
    if (wasOpen)
        m_cv.notify_all();
}

bool JackMidiDevice::send(const QByteArray &bytes, QString *error)
{
    if (!m_open.load() || !m_client) {
        if (error)
            *error = QStringLiteral("The PipeWire MIDI device is not open.");
        return false;
    }
    if (bytes.isEmpty()) {
        if (error)
            *error = QStringLiteral("Cannot send an empty MIDI message.");
        return false;
    }

    auto item = std::make_shared<Outgoing>();
    item->data = bytes;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tx.push_back(item);
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    const bool completed = m_cv.wait_for(lock, std::chrono::seconds(2),
                                         [&item, this] {
                                             return item->done || !m_open.load();
                                         });
    if (!completed || !item->done) {
        if (error)
            *error = QStringLiteral("Timed out sending a MIDI message over PipeWire.");
        return false;
    }
    if (item->result != 0) {
        if (error) {
            *error = QStringLiteral("PipeWire MIDI write failed (error %1).")
                         .arg(item->result);
        }
        return false;
    }
    return true;
}

QByteArray JackMidiDevice::receive(int timeoutMs, QString *error)
{
    if (!m_open.load() || !m_client) {
        if (error)
            *error = QStringLiteral("The PipeWire MIDI device is not open.");
        return {};
    }

    QElapsedTimer timer;
    timer.start();
    while (true) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const QByteArray frame = takeSysExFrame(&m_buffer);
            if (!frame.isEmpty())
                return frame;
        }

        const qint64 remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            if (error)
                *error = QStringLiteral("Timed out waiting for a MIDI SysEx reply over PipeWire.");
            return {};
        }

        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, std::chrono::milliseconds(remaining),
                      [this] { return !m_buffer.isEmpty() || !m_open.load(); });
        if (!m_open.load()) {
            if (error)
                *error = QStringLiteral("The PipeWire MIDI device was closed.");
            return {};
        }
    }
}

void JackMidiDevice::discardInput()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear();
}

int JackMidiDevice::processCallback(jack_nframes_t frames, void *arg)
{
    static_cast<JackMidiDevice *>(arg)->process(frames);
    return 0;
}

void JackMidiDevice::process(jack_nframes_t frames)
{
    if (!m_open.load() || !m_in || !m_out)
        return;

    void *outBuffer = jack_port_get_buffer(m_out, frames);
    if (outBuffer) {
        jack_midi_clear_buffer(outBuffer);
        std::deque<std::shared_ptr<Outgoing>> pending;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            pending.swap(m_tx);
        }
        for (const auto &item : pending) {
            const int result = writeMidiEvent(outBuffer, item->data);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                item->result = result;
                item->done = true;
            }
            m_cv.notify_all();
        }
    }

    void *inBuffer = jack_port_get_buffer(m_in, frames);
    if (!inBuffer)
        return;
    const uint32_t count = jack_midi_get_event_count(inBuffer);
    if (count == 0)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (uint32_t i = 0; i < count; ++i) {
        jack_midi_event_t event;
        if (jack_midi_event_get(&event, inBuffer, i) == 0 && event.size > 0)
            m_buffer.append(reinterpret_cast<const char *>(event.buffer), int(event.size));
    }
    m_cv.notify_all();
}

int JackMidiDevice::writeMidiEvent(void *buffer, const QByteArray &data)
{
    const int result = jack_midi_event_write(
        buffer, 0, reinterpret_cast<const jack_midi_data_t *>(data.constData()),
        size_t(data.size()));
    if (result != ENOSPC)
        return result;

    // PipeWire's JACK layer accepts the current 2.4 KB SysEx messages whole,
    // but keep a chunked fallback for smaller MIDI buffers.
    const int chunkSize = 1024;
    for (int offset = 0; offset < data.size(); offset += chunkSize) {
        const int size = qMin(chunkSize, data.size() - offset);
        const int chunkResult = jack_midi_event_write(
            buffer, 0,
            reinterpret_cast<const jack_midi_data_t *>(data.constData() + offset),
            size_t(size));
        if (chunkResult != 0)
            return chunkResult;
    }
    return 0;
}

} // namespace korg
