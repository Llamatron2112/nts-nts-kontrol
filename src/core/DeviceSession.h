#pragma once

#include "core/midi/RawMidiDevice.h"
#include "core/model/Program.h"
#include "core/model/UserUnit.h"
#include "core/protocol/KorgProtocol.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <memory>

namespace korg {

struct DeviceInfo {
    MidiPort port;
    int channel = -1;
    quint16 family = 0;
    quint16 member = 0;
    quint16 majorVersion = 0;
    quint16 minorVersion = 0;
    bool valid = false;

    QString displayName() const;
    QString versionString() const;
};

struct SlotInfo {
    bool present = false;
    bool valid = false;
    UnitHeader header;
    QString note;
};

class DeviceSession
{
public:
    DeviceSession() = default;
    ~DeviceSession();

    DeviceSession(const DeviceSession &) = delete;
    DeviceSession &operator=(const DeviceSession &) = delete;

    static QList<DeviceInfo> scan(
        MidiBackendPreference preference = MidiBackendPreference::Auto,
        QString *error = nullptr);
    static bool pipeWireAvailable();

    bool open(const DeviceInfo &info, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_open; }

    const DeviceInfo &info() const { return m_info; }
    int channel() const { return m_info.channel; }

    bool identify(QString *error = nullptr);
    bool getApiVersion(quint8 *platform, int *major, int *minor, int *patch,
                       QString *error = nullptr);

    bool receiveProgram(Program *program, QString *error = nullptr);
    bool sendProgram(const Program &program, QString *error = nullptr);

    bool receiveGlobal(QByteArray *global, QString *error = nullptr);
    bool sendGlobal(const QByteArray &global, QString *error = nullptr);

    bool getSlotStatus(Module module, int slot, SlotInfo *info, QString *error = nullptr);
    bool downloadUnit(Module module, int slot, QByteArray *unit, QString *error = nullptr,
                      const std::function<void(int, int)> &progress = {});
    bool uploadUnit(Module module, int slot, const QByteArray &unit, QString *error = nullptr,
                    const std::function<void(int, int)> &progress = {});
    bool clearSlot(Module module, int slot, QString *error = nullptr);
    bool swapSlots(Module module, int slotA, int slotB, QString *error = nullptr);

private:
    bool transact(const QByteArray &request, int expectedId, ParsedSysex *reply,
                  QString *error, int timeoutMs = 3000);
    bool sendAndAwaitAck(const QByteArray &message, QString *error, int timeoutMs = 5000);
    bool sendNoAck(const QByteArray &message, QString *error, int timeoutMs = 300);
    bool receiveLogue(int expectedId, ParsedSysex *reply, QString *error, int timeoutMs);

    std::unique_ptr<MidiBackend> m_midi;
    DeviceInfo m_info;
    bool m_open = false;
};

} // namespace korg
