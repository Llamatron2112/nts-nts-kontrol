#include "core/midi/MidiBackend.h"

namespace korg {

QString midiBackendPreferenceToString(MidiBackendPreference preference)
{
    switch (preference) {
    case MidiBackendPreference::PipeWire: return QStringLiteral("pipewire");
    case MidiBackendPreference::AlsaRaw:  return QStringLiteral("alsa");
    case MidiBackendPreference::Auto:     break;
    }
    return QStringLiteral("auto");
}

MidiBackendPreference midiBackendPreferenceFromString(const QString &value)
{
    const QString lower = value.trimmed().toLower();
    if (lower == QStringLiteral("pipewire") || lower == QStringLiteral("jack"))
        return MidiBackendPreference::PipeWire;
    if (lower == QStringLiteral("alsa") || lower == QStringLiteral("raw"))
        return MidiBackendPreference::AlsaRaw;
    return MidiBackendPreference::Auto;
}

bool midiPipeWireAvailable()
{
#ifdef HAVE_JACK
    return true;
#else
    return false;
#endif
}

QString MidiPort::displayName() const
{
    if (backend == MidiBackendType::PipeWireJack) {
        if (!cardName.isEmpty())
            return cardName;
        return QStringLiteral("NTS-1 mkII (PipeWire)");
    }

    if (!portName.isEmpty() && portName != cardName)
        return QStringLiteral("%1 (%2)").arg(cardName, portName);
    return cardName;
}

QString MidiPort::hwName() const
{
    if (backend == MidiBackendType::PipeWireJack)
        return QStringLiteral("pipewire");
    return QStringLiteral("hw:%1,%2,%3").arg(card).arg(device).arg(subdevice);
}

QString MidiPort::backendName() const
{
    return backend == MidiBackendType::PipeWireJack ? QStringLiteral("PipeWire")
                                                    : QStringLiteral("ALSA raw");
}

} // namespace korg
