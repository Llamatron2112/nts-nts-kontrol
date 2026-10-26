#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace korg {

constexpr quint8 korgId = 0x42;
constexpr quint8 familyIdLsb = 0x73;
constexpr quint8 familyIdMsb = 0x01;
constexpr quint8 memberIdLsb = 0x01;
constexpr quint8 memberIdMsb = 0x00;
constexpr quint8 nts1Mk2PlatformId = 5;

enum class Module : quint8 {
    ModFx = 1,
    DelFx = 2,
    RevFx = 3,
    Osc = 4,
};

inline int slotCount(Module module)
{
    return (module == Module::Osc || module == Module::ModFx) ? 16 : 8;
}

inline const char *moduleName(Module module)
{
    switch (module) {
    case Module::Osc:   return "osc";
    case Module::ModFx: return "modfx";
    case Module::DelFx: return "delfx";
    case Module::RevFx: return "revfx";
    }
    return "?";
}

inline QString moduleDisplayName(Module module)
{
    switch (module) {
    case Module::Osc:   return QStringLiteral("Oscillator");
    case Module::ModFx: return QStringLiteral("Modulation FX");
    case Module::DelFx: return QStringLiteral("Delay FX");
    case Module::RevFx: return QStringLiteral("Reverb FX");
    }
    return QStringLiteral("?");
}

QByteArray encode7bit(const QByteArray &host);
QByteArray decode7bit(const QByteArray &midi);
quint32 crc32(const QByteArray &data);

QByteArray sysexFrame(quint8 channel, quint8 id, const QByteArray &payload = {});
QByteArray searchRequest(quint8 echoId = 0x55);
QByteArray identityRequest(quint8 deviceId = 0x7f);
QByteArray currentProgramRequest(quint8 channel);
QByteArray currentProgramDump(quint8 channel, const QByteArray &program504);
QByteArray globalRequest(quint8 channel);
QByteArray globalDump(quint8 channel, const QByteArray &global32);
QByteArray userApiVersionRequest(quint8 channel);
QByteArray userModuleInfoRequest(quint8 channel, Module module);
QByteArray userSlotStatusRequest(quint8 channel, Module module, int slot);
QByteArray userSlotDataRequest(quint8 channel, Module module, int slot);
QByteArray clearUserSlot(quint8 channel, Module module, int slot);
QByteArray clearUserModule(quint8 channel, Module module);
QByteArray swapUserSlots(quint8 channel, Module module, int slotA, int slotB);
QList<QByteArray> userSlotDataMessages(quint8 channel, Module module, int slot,
                                        const QByteArray &unitFile);

struct ParsedSysex {
    enum class Kind {
        Invalid,
        Logue,
        SearchReply,
        IdentityReply,
        Unknown,
    };

    Kind kind = Kind::Invalid;
    QByteArray raw;
    QByteArray data;     // payload without F0/F7
    quint8 id = 0;
    int channel = -1;    // zero based global MIDI channel
    QByteArray payload;  // message-specific payload (after id)

    bool isLogue() const { return kind == Kind::Logue; }
    bool isStatus() const { return isLogue() && id >= 0x23 && id <= 0x2f; }
    bool isCompleted() const { return isLogue() && id == 0x23; }
};

ParsedSysex parseSysex(const QByteArray &frame);
QString statusDescription(quint8 id);

struct SearchReply {
    bool valid = false;
    int channel = -1;
    quint8 echoId = 0;
    quint16 family = 0;
    quint16 member = 0;
    quint16 minorVersion = 0;
    quint16 majorVersion = 0;
};

SearchReply parseSearchReply(const ParsedSysex &message);

} // namespace korg
