#include "core/protocol/KorgProtocol.h"

#include <array>

namespace korg {

namespace {

constexpr int kMaxSysexSize = 4096;
constexpr int kUserSlotHeaderSize = 8;   // length + crc32
constexpr int kUserSlotMaxHost = 2048;   // NTS-1 mkII rejects larger chunks

void appendLe32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 24) & 0xff));
}

quint32 readLe32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return quint32(quint8(data[offset]))
            | (quint32(quint8(data[offset + 1])) << 8)
            | (quint32(quint8(data[offset + 2])) << 16)
            | (quint32(quint8(data[offset + 3])) << 24);
}

} // namespace

QByteArray encode7bit(const QByteArray &host)
{
    QByteArray out;
    out.reserve(((host.size() + 6) / 7) * 8);
    for (int i = 0; i < host.size(); i += 7) {
        const int end = qMin(i + 7, host.size());
        quint8 msb = 0;
        for (int bit = 0; bit < end - i; ++bit)
            msb |= quint8((quint8(host[i + bit]) >> 7) & 1) << bit;
        out.append(char(msb));
        for (int j = i; j < end; ++j)
            out.append(char(quint8(host[j]) & 0x7f));
    }
    return out;
}

QByteArray decode7bit(const QByteArray &midi)
{
    QByteArray out;
    out.reserve((midi.size() / 8) * 7 + 7);
    for (int i = 0; i < midi.size(); i += 8) {
        const quint8 msb = quint8(midi[i]);
        const int end = qMin(i + 8, midi.size());
        for (int bit = 0; bit < end - i - 1; ++bit) {
            const quint8 value = quint8(midi[i + 1 + bit]);
            out.append(char(value | quint8((msb >> bit) & 1) << 7));
        }
    }
    return out;
}

quint32 crc32(const QByteArray &data)
{
    static std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t {};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();

    quint32 crc = 0xffffffffu;
    for (char ch : data)
        crc = table[(crc ^ quint8(ch)) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

QByteArray sysexFrame(quint8 channel, quint8 id, const QByteArray &payload)
{
    QByteArray out;
    out.reserve(payload.size() + 8);
    out.append(char(0xf0));
    out.append(char(korgId));
    out.append(char(quint8(0x30) | (channel & 0x0f)));
    out.append(char(0x00));
    out.append(char(0x01));
    out.append(char(familyIdLsb));
    out.append(char(id));
    out.append(payload);
    out.append(char(0xf7));
    return out;
}

QByteArray searchRequest(quint8 echoId)
{
    QByteArray out;
    out.append(char(0xf0));
    out.append(char(korgId));
    out.append(char(0x50));
    out.append(char(0x00));
    out.append(char(echoId & 0x7f));
    out.append(char(0xf7));
    return out;
}

QByteArray identityRequest(quint8 deviceId)
{
    QByteArray out;
    out.append(char(0xf0));
    out.append(char(0x7e));
    out.append(char(deviceId & 0x7f));
    out.append(char(0x06));
    out.append(char(0x01));
    out.append(char(0xf7));
    return out;
}

QByteArray currentProgramRequest(quint8 channel)
{
    return sysexFrame(channel, 0x10);
}

QByteArray currentProgramDump(quint8 channel, const QByteArray &program504)
{
    if (program504.size() != 504)
        return {};
    return sysexFrame(channel, 0x40, encode7bit(program504));
}

QByteArray globalRequest(quint8 channel)
{
    return sysexFrame(channel, 0x0e);
}

QByteArray globalDump(quint8 channel, const QByteArray &global32)
{
    if (global32.size() != 32)
        return {};
    return sysexFrame(channel, 0x51, encode7bit(global32));
}

QByteArray userApiVersionRequest(quint8 channel)
{
    return sysexFrame(channel, 0x17);
}

QByteArray userModuleInfoRequest(quint8 channel, Module module)
{
    QByteArray payload;
    payload.append(char(quint8(module)));
    return sysexFrame(channel, 0x18, payload);
}

QByteArray userSlotStatusRequest(quint8 channel, Module module, int slot)
{
    QByteArray payload;
    payload.append(char(quint8(module)));
    payload.append(char(slot & 0x7f));
    return sysexFrame(channel, 0x19, payload);
}

QByteArray userSlotDataRequest(quint8 channel, Module module, int slot)
{
    QByteArray payload;
    payload.append(char(quint8(module)));
    payload.append(char(slot & 0x7f));
    return sysexFrame(channel, 0x1a, payload);
}

QByteArray clearUserSlot(quint8 channel, Module module, int slot)
{
    QByteArray payload;
    payload.append(char(quint8(module)));
    payload.append(char(slot & 0x7f));
    return sysexFrame(channel, 0x1b, payload);
}

QByteArray clearUserModule(quint8 channel, Module module)
{
    QByteArray payload;
    payload.append(char(quint8(module)));
    return sysexFrame(channel, 0x1d, payload);
}

QByteArray swapUserSlots(quint8 channel, Module module, int slotA, int slotB)
{
    QByteArray payload;
    payload.append(char(quint8(module)));
    payload.append(char(slotA & 0x7f));
    payload.append(char(slotB & 0x7f));
    return sysexFrame(channel, 0x1e, payload);
}

QList<QByteArray> userSlotDataMessages(quint8 channel, Module module, int slot,
                                        const QByteArray &unitFile)
{
    QList<QByteArray> messages;
    if (unitFile.isEmpty() || slot < 0 || slot >= slotCount(module))
        return messages;

    QByteArray host;
    host.reserve(unitFile.size() + kUserSlotHeaderSize);
    appendLe32(host, quint32(unitFile.size()));
    appendLe32(host, crc32(unitFile));
    host.append(unitFile);

    const int sequenceMax = (host.size() - 1) / kUserSlotMaxHost;
    for (int seq = 0; seq <= sequenceMax; ++seq) {
        const int begin = seq * kUserSlotMaxHost;
        const int end = qMin(begin + kUserSlotMaxHost, host.size());
        QByteArray payload;
        payload.append(char(quint8(module)));
        payload.append(char(slot & 0x7f));
        payload.append(char(seq & 0x7f));
        payload.append(char(sequenceMax & 0x7f));
        payload.append(encode7bit(host.mid(begin, end - begin)));
        const QByteArray frame = sysexFrame(channel, 0x4a, payload);
        if (frame.size() > kMaxSysexSize) {
            messages.clear();
            return messages;
        }
        messages.append(frame);
    }
    return messages;
}

ParsedSysex parseSysex(const QByteArray &frame)
{
    ParsedSysex result;
    result.raw = frame;
    if (frame.size() < 3 || quint8(frame.front()) != 0xf0
        || quint8(frame.back()) != 0xf7)
        return result;

    result.data = frame.mid(1, frame.size() - 2);
    if (result.data.size() < 2)
        return result;

    const quint8 first = quint8(result.data[0]);
    if (first == 0x7e) {
        result.kind = ParsedSysex::Kind::IdentityReply;
        return result;
    }

    if (first != korgId)
        return result;

    const quint8 second = quint8(result.data[1]);
    if (second == 0x50 && result.data.size() >= 4) {
        result.kind = ParsedSysex::Kind::SearchReply;
        result.id = 0x50;
        result.channel = quint8(result.data[3]) & 0x0f;
        result.payload = result.data.mid(3);
        return result;
    }

    if (second >= 0x30 && second <= 0x3f && result.data.size() >= 6
        && quint8(result.data[2]) == 0x00
        && quint8(result.data[3]) == 0x01
        && quint8(result.data[4]) == familyIdLsb) {
        result.kind = ParsedSysex::Kind::Logue;
        result.channel = second & 0x0f;
        result.id = quint8(result.data[5]);
        result.payload = result.data.mid(6);
        return result;
    }

    result.kind = ParsedSysex::Kind::Unknown;
    return result;
}

QString statusDescription(quint8 id)
{
    switch (id) {
    case 0x23: return QStringLiteral("Operation completed.");
    case 0x24: return QStringLiteral("Operation error.");
    case 0x26: return QStringLiteral("Data format error.");
    case 0x27: return QStringLiteral("User data size error.");
    case 0x28: return QStringLiteral("User data CRC error.");
    case 0x29: return QStringLiteral("User target error.");
    case 0x2a: return QStringLiteral("User API version error.");
    case 0x2b: return QStringLiteral("User load size error.");
    case 0x2c: return QStringLiteral("User module error.");
    case 0x2d: return QStringLiteral("User slot error.");
    case 0x2e: return QStringLiteral("User format error.");
    case 0x2f: return QStringLiteral("User internal error.");
    default:   return QStringLiteral("Unknown status 0x%1.").arg(id, 2, 16, QLatin1Char('0'));
    }
}

SearchReply parseSearchReply(const ParsedSysex &message)
{
    SearchReply reply;
    const QByteArray &d = message.data;
    if (message.kind != ParsedSysex::Kind::SearchReply || d.size() < 13)
        return reply;

    reply.valid = true;
    reply.channel = quint8(d[3]) & 0x0f;
    reply.echoId = quint8(d[4]);
    reply.family = quint16(quint8(d[5])) | (quint16(quint8(d[6])) << 8);
    reply.member = quint16(quint8(d[7])) | (quint16(quint8(d[8])) << 8);
    reply.minorVersion = quint16(quint8(d[9])) | (quint16(quint8(d[10])) << 8);
    reply.majorVersion = quint16(quint8(d[11])) | (quint16(quint8(d[12])) << 8);
    return reply;
}

} // namespace korg
