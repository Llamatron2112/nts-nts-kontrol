#pragma once

#include "core/midi/MidiBackend.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace korg {

class RawMidiDevice : public MidiBackend
{
public:
    RawMidiDevice() = default;
    ~RawMidiDevice() override;

    RawMidiDevice(const RawMidiDevice &) = delete;
    RawMidiDevice &operator=(const RawMidiDevice &) = delete;

    static QList<MidiPort> enumerate();

    bool open(const MidiPort &port, QString *error = nullptr) override;
    void close() override;
    bool isOpen() const override { return m_midiFd >= 0; }

    bool send(const QByteArray &bytes, QString *error = nullptr) override;
    QByteArray receive(int timeoutMs, QString *error = nullptr) override;
    void discardInput() override;

    MidiPort port() const { return m_port; }

private:
    bool writeAll(const char *data, qsizetype size, QString *error);
    bool configureStream(int stream);

    MidiPort m_port;
    int m_controlFd = -1;
    int m_midiFd = -1;
    QByteArray m_buffer;
};

} // namespace korg
