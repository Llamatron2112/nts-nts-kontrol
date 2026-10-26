#pragma once

#include "core/midi/MidiBackend.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

#include <jack/jack.h>
#include <jack/midiport.h>

namespace korg {

class JackMidiDevice : public MidiBackend
{
public:
    JackMidiDevice() = default;
    ~JackMidiDevice() override;

    JackMidiDevice(const JackMidiDevice &) = delete;
    JackMidiDevice &operator=(const JackMidiDevice &) = delete;

    static QList<MidiPort> enumerate();

    bool open(const MidiPort &port, QString *error = nullptr) override;
    void close() override;
    bool isOpen() const override { return m_open.load(); }

    bool send(const QByteArray &bytes, QString *error = nullptr) override;
    QByteArray receive(int timeoutMs, QString *error = nullptr) override;
    void discardInput() override;

    MidiPort port() const { return m_port; }

private:
    struct Outgoing {
        QByteArray data;
        bool done = false;
        int result = 0;
    };

    static int processCallback(jack_nframes_t frames, void *arg);
    void process(jack_nframes_t frames);
    int writeMidiEvent(void *buffer, const QByteArray &data);

    jack_client_t *m_client = nullptr;
    jack_port_t *m_in = nullptr;
    jack_port_t *m_out = nullptr;
    MidiPort m_port;
    std::atomic<bool> m_open {false};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<Outgoing>> m_tx;
    QByteArray m_buffer;
};

} // namespace korg
