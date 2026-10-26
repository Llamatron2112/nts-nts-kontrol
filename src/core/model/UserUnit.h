#pragma once

#include "core/protocol/KorgProtocol.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace korg {

struct UnitParam {
    qint16 min = 0;
    qint16 max = 0;
    qint16 center = 0;
    qint16 init = 0;
    quint8 type = 0;
    quint8 flags = 0;
    QString name;
};

struct UnitHeader {
    static constexpr int kSize = 408;
    static constexpr int kParamSize = 32;
    static constexpr int kMaxParams = 11;

    quint32 headerSize = 0;
    quint32 target = 0;
    quint32 api = 0;
    quint32 devId = 0;
    quint32 unitId = 0;
    quint32 version = 0;
    QString name;
    quint32 reserved0 = 0;
    quint32 reserved1 = 0;
    quint32 numParams = 0;
    QList<UnitParam> params;

    bool valid = false;
    int platform() const { return int((target >> 8) & 0xff); }
    int moduleBits() const { return int(target & 0xff); }
    Module module() const;
    bool hasModule() const;
    QString moduleName() const;
    QString devIdString() const;
    QString unitIdString() const;
    QString apiString() const;
    QString versionString() const;
};

class UserUnit
{
public:
    UserUnit() = default;
    explicit UserUnit(const QByteArray &elfBytes);

    static UserUnit fromBytes(const QByteArray &bytes, bool *ok = nullptr);

    bool isValid() const { return m_header.valid; }
    bool isEmpty() const { return m_data.isEmpty(); }
    const QByteArray &bytes() const { return m_data; }
    QByteArray &bytes() { return m_data; }
    const UnitHeader &header() const { return m_header; }
    UnitHeader &header() { return m_header; }

    QString displayName() const;
    QString targetDescription() const;

private:
    static bool parseElf(const QByteArray &elf, UnitHeader *header);
    static bool parseHeaderAt(const QByteArray &data, int offset, UnitHeader *header);

    QByteArray m_data;
    UnitHeader m_header;
};

} // namespace korg
