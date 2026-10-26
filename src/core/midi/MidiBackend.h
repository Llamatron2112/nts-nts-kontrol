#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace korg {

enum class MidiBackendType {
    AlsaRaw,
    PipeWireJack,
};

enum class MidiBackendPreference {
    Auto,
    PipeWire,
    AlsaRaw,
};

QString midiBackendPreferenceToString(MidiBackendPreference preference);
MidiBackendPreference midiBackendPreferenceFromString(const QString &value);
bool midiPipeWireAvailable();

struct MidiPort {
    MidiBackendType backend = MidiBackendType::AlsaRaw;
    bool isKorg = false;

    // Kernel ALSA raw MIDI endpoint.
    int card = -1;
    int device = -1;
    int subdevice = -1;
    QString cardId;
    QString cardName;
    QString portName;
    QString devicePath;
    QString controlPath;

    // PipeWire JACK MIDI endpoint.
    QString jackPlayback;
    QString jackCapture;
    QString jackDeviceKey;
    QString jackVariant;

    QString displayName() const;
    QString hwName() const;
    QString backendName() const;
};

class MidiBackend
{
public:
    virtual ~MidiBackend() = default;

    virtual bool open(const MidiPort &port, QString *error = nullptr) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    virtual bool send(const QByteArray &bytes, QString *error = nullptr) = 0;
    virtual QByteArray receive(int timeoutMs, QString *error = nullptr) = 0;
    virtual void discardInput() = 0;
};

} // namespace korg
