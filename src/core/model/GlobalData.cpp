#include "core/model/GlobalData.h"

#include <algorithm>

namespace korg {

namespace {

const QByteArray kWorkspaceDefault = QByteArray::fromHex(
    "000101000703000101000100000f06000100000100000100000101000101000100000100000f00000100000101000101");

} // namespace

GlobalData::GlobalData()
    : m_workspace(kWorkspaceDefault)
{
}

GlobalData GlobalData::fromDeviceBytes(const QByteArray &bytes, bool *ok)
{
    GlobalData data;
    if (looksLikeDeviceData(bytes))
        data.m_device = bytes;
    if (ok)
        *ok = data.isValid();
    return data;
}

bool GlobalData::looksLikeDeviceData(const QByteArray &bytes)
{
    return bytes.size() == kDeviceSize
           && bytes.mid(0, 4) == QByteArrayLiteral("GLOB")
           && bytes.mid(kDeviceSize - 4, 4) == QByteArrayLiteral("GLED");
}

QByteArray GlobalData::defaultDeviceData()
{
    QByteArray bytes(kDeviceSize, '\0');
    bytes.replace(0, 4, "GLOB");
    bytes.replace(kDeviceSize - 4, 4, "GLED");
    return bytes;
}

QByteArray GlobalData::defaultWorkspaceData()
{
    return kWorkspaceDefault;
}

bool GlobalData::isValid() const
{
    return looksLikeDeviceData(m_device);
}

void GlobalData::setWorkspaceBytes(const QByteArray &bytes)
{
    m_workspace = bytes;
    if (m_workspace.size() != kWorkspaceSize)
        m_workspace = kWorkspaceDefault;
}

int GlobalData::value(int offset) const
{
    if (offset < 0 || offset >= m_device.size())
        return 0;
    return quint8(m_device[offset]);
}

void GlobalData::setValue(int offset, int value)
{
    if (offset < 0 || offset >= m_device.size())
        return;
    m_device[offset] = char(value & 0xff);
}

int GlobalData::midiRoute() const
{
    return value(14);
}

bool GlobalData::usbMidiDisabled() const
{
    // 0 = USB+MIDI, 1 = USB, 2 = MIDI only.
    return midiRoute() == 2;
}

int GlobalData::midiChannel() const
{
    return value(15);
}

QList<GlobalField> GlobalData::fields()
{
    // The NTS-1 mkII global blob is: "GLOB" + 15 one-byte settings +
    // reserved 0xFF bytes + "GLED". Offsets below refer to the full 32-byte blob.
    QList<GlobalField> fields;
    auto add = [&fields](int offset, const QString &key, const QString &label,
                         const QStringList &options, const QString &help = {}) {
        GlobalField field;
        field.offset = offset;
        field.key = key;
        field.label = label;
        field.enumNames = options;
        field.help = help;
        if (!options.isEmpty()) {
            field.minimum = 0;
            field.maximum = options.size() - 1;
        }
        fields.append(field);
    };

    add(4, QStringLiteral("input_route"), QStringLiteral("Input Route"),
        {QStringLiteral("OFF"), QStringLiteral("PRE FILTER"), QStringLiteral("PRE AMP"),
         QStringLiteral("PRE MODULATION"), QStringLiteral("PRE DELAY"),
         QStringLiteral("PRE REVERB"), QStringLiteral("PRE MASTER"),
         QStringLiteral("POST MASTER")});
    add(5, QStringLiteral("input_route_cut"), QStringLiteral("Input Route Cut"),
        {QStringLiteral("DISABLE"), QStringLiteral("ENABLE")});
    add(6, QStringLiteral("input_gain"), QStringLiteral("Input Gain"),
        {QStringLiteral("-85 dB"), QStringLiteral("-36 dB"), QStringLiteral("-24 dB"),
         QStringLiteral("-18 dB"), QStringLiteral("-12 dB"), QStringLiteral("-9 dB"),
         QStringLiteral("-6 dB"), QStringLiteral("-3 dB"), QStringLiteral("-2 dB"),
         QStringLiteral("-1 dB"), QStringLiteral("0 dB"), QStringLiteral("1 dB"),
         QStringLiteral("2 dB"), QStringLiteral("3 dB"), QStringLiteral("6 dB"),
         QStringLiteral("9 dB")});
    add(7, QStringLiteral("monaural"), QStringLiteral("Monaural"),
        {QStringLiteral("OFF"), QStringLiteral("MONAURAL")});
    add(8, QStringLiteral("sync_out_polarity"), QStringLiteral("Sync Out Polarity"),
        {QStringLiteral("ACTIVE HIGH"), QStringLiteral("ACTIVE LOW")});
    add(9, QStringLiteral("sync_in_polarity"), QStringLiteral("Sync In Polarity"),
        {QStringLiteral("ACTIVE HIGH"), QStringLiteral("ACTIVE LOW")});
    add(10, QStringLiteral("tempo_range"), QStringLiteral("Tempo Range"),
        {QStringLiteral("NARROW"), QStringLiteral("WIDE")});
    add(11, QStringLiteral("clock_source"), QStringLiteral("Clock Source"),
        {QStringLiteral("INTERNAL"), QStringLiteral("AUTO"), QStringLiteral("USB")});
    add(12, QStringLiteral("short_message"), QStringLiteral("Short Message"),
        {QStringLiteral("OFF"), QStringLiteral("ON")});
    add(13, QStringLiteral("nrpn_messages"), QStringLiteral("NRPN Messages"),
        {QStringLiteral("OFF"), QStringLiteral("ON")});
    add(14, QStringLiteral("midi_route"), QStringLiteral("MIDI Route"),
        {QStringLiteral("USB+MIDI"), QStringLiteral("USB"), QStringLiteral("MIDI")},
        QStringLiteral("MIDI disables USB MIDI. The editor will lose its "
                       "connection until MIDI Route is changed on the device."));
    {
        GlobalField midi;
        midi.offset = 15;
        midi.key = QStringLiteral("midi_channel");
        midi.label = QStringLiteral("MIDI Channel");
        midi.minimum = 0;
        midi.maximum = 15;
        midi.displayOffset = 1;
        midi.help = QStringLiteral("Zero-based in the global blob; displayed as 1-16.");
        fields.append(midi);
    }
    add(16, QStringLiteral("sync_unit"), QStringLiteral("Sync Unit"),
        {QStringLiteral("2STEP"), QStringLiteral("EVERY STEP")});
    add(17, QStringLiteral("legato_mode"), QStringLiteral("Legato Mode"),
        {QStringLiteral("OFF"), QStringLiteral("ON")});
    add(18, QStringLiteral("auto_power_off"), QStringLiteral("Auto Power Off"),
        {QStringLiteral("OFF"), QStringLiteral("ON")});
    return fields;
}

} // namespace korg
