#include "core/model/UserUnit.h"

#include <QStringList>

#include <algorithm>
#include <cstring>

namespace korg {

namespace {

constexpr quint32 kHeaderMagic = 0x198;
constexpr quint32 kApi2 = 0x00020000;
constexpr quint32 kPlatform = 5;

quint16 readU16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return quint16(quint8(data[offset])) | (quint16(quint8(data[offset + 1])) << 8);
}

quint32 readU32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return quint32(quint8(data[offset]))
            | (quint32(quint8(data[offset + 1])) << 8)
            | (quint32(quint8(data[offset + 2])) << 16)
            | (quint32(quint8(data[offset + 3])) << 24);
}

qint16 readS16(const QByteArray &data, int offset)
{
    return qint16(readU16(data, offset));
}

QString readAscii(const QByteArray &data, int offset, int length)
{
    if (offset < 0 || length <= 0 || offset + length > data.size())
        return {};
    QByteArray slice = data.mid(offset, length);
    const int nul = slice.indexOf('\0');
    if (nul >= 0)
        slice.truncate(nul);
    return QString::fromLatin1(slice).trimmed();
}

QString formatVersion(quint32 version)
{
    const int major = int((version >> 16) & 0xff);
    const int minor = int((version >> 8) & 0xff);
    const int patch = int(version & 0xff);
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

bool plausibleHeader(const UnitHeader &header)
{
    if (header.headerSize != UnitHeader::kSize)
        return false;
    if (header.numParams > UnitHeader::kMaxParams)
        return false;
    if (header.platform() != kPlatform)
        return false;
    if ((header.api >> 16) == 0 || (header.api >> 16) > 3)
        return false;
    return true;
}

} // namespace

Module UnitHeader::module() const
{
    switch (moduleBits()) {
    case 1: return Module::ModFx;
    case 2: return Module::DelFx;
    case 3: return Module::RevFx;
    case 4: return Module::Osc;
    default: return Module::Osc;
    }
}

bool UnitHeader::hasModule() const
{
    const int bits = moduleBits();
    return bits >= 1 && bits <= 4;
}

QString UnitHeader::moduleName() const
{
    if (!hasModule())
        return QStringLiteral("Unknown");
    return moduleDisplayName(module());
}

QString UnitHeader::devIdString() const
{
    return QStringLiteral("0x%1").arg(devId, 8, 16, QLatin1Char('0'));
}

QString UnitHeader::unitIdString() const
{
    return QStringLiteral("0x%1").arg(unitId, 8, 16, QLatin1Char('0'));
}

QString UnitHeader::apiString() const
{
    return formatVersion(api);
}

QString UnitHeader::versionString() const
{
    return formatVersion(version);
}

UserUnit::UserUnit(const QByteArray &elfBytes)
    : m_data(elfBytes)
{
    parseElf(m_data, &m_header);
}

UserUnit UserUnit::fromBytes(const QByteArray &bytes, bool *ok)
{
    UserUnit unit(bytes);
    if (ok)
        *ok = unit.isValid();
    return unit;
}

bool UserUnit::parseElf(const QByteArray &elf, UnitHeader *header)
{
    *header = {};
    bool bestFound = false;
    UnitHeader best;

    auto consider = [&](const UnitHeader &candidate) {
        if (!candidate.valid)
            return;
        if (!bestFound) {
            best = candidate;
            bestFound = true;
            return;
        }
        const bool candidateHasModule = candidate.hasModule();
        const bool bestHasModule = best.hasModule();
        if (candidateHasModule && !bestHasModule)
            best = candidate;
        else if (candidateHasModule == bestHasModule
                 && candidate.name.compare(QStringLiteral("undefined"), Qt::CaseInsensitive) != 0
                 && best.name.compare(QStringLiteral("undefined"), Qt::CaseInsensitive) == 0)
            best = candidate;
    };

    if (elf.size() >= 52 && elf.startsWith("\x7f" "ELF") && quint8(elf[4]) == 1) {
        const quint32 shoff = readU32(elf, 32);
        const quint16 shentsize = readU16(elf, 46);
        const quint16 shnum = readU16(elf, 48);
        const quint16 shstrndx = readU16(elf, 50);

        if (shoff > 0 && shentsize >= 40 && shnum > 0
            && qint64(shoff) + qint64(shentsize) * shnum <= elf.size()) {
            QByteArray sectionNames;
            if (shstrndx < shnum) {
                const int strOff = int(shoff) + int(shstrndx) * int(shentsize);
                const quint32 namesOffset = readU32(elf, strOff + 16);
                const quint32 namesSize = readU32(elf, strOff + 20);
                if (qint64(namesOffset) + qint64(namesSize) <= elf.size())
                    sectionNames = elf.mid(int(namesOffset), int(namesSize));
            }

            for (int i = 0; i < shnum; ++i) {
                const int base = int(shoff) + i * int(shentsize);
                const quint32 nameOffset = readU32(elf, base);
                const quint32 sectionOffset = readU32(elf, base + 16);
                const quint32 sectionSize = readU32(elf, base + 20);
                if (!sectionNames.isEmpty()) {
                    const int end = sectionNames.indexOf('\0', int(nameOffset));
                    const QByteArray name = sectionNames.mid(
                        int(nameOffset), end < 0 ? -1 : end - int(nameOffset));
                    if (name != QByteArrayLiteral(".unit_header"))
                        continue;
                } else {
                    continue;
                }
                if (sectionOffset >= quint32(elf.size()))
                    continue;
                const int available = int(qMin<quint32>(sectionSize,
                                                       quint32(elf.size()) - sectionOffset));
                for (int pos = 0; pos + UnitHeader::kSize <= available;
                     pos += UnitHeader::kSize) {
                    UnitHeader candidate;
                    if (parseHeaderAt(elf.mid(int(sectionOffset), available), pos, &candidate))
                        consider(candidate);
                }
            }
        }

        // If the section table is missing/corrupted, use program headers and scan
        // the loadable segments for a plausible header.
        if (!bestFound) {
            const quint32 phoff = readU32(elf, 28);
            const quint16 phentsize = readU16(elf, 42);
            const quint16 phnum = readU16(elf, 44);
            if (phoff > 0 && phentsize >= 32 && phnum > 0
                && qint64(phoff) + qint64(phentsize) * phnum <= elf.size()) {
                for (int i = 0; i < phnum; ++i) {
                    const int base = int(phoff) + i * int(phentsize);
                    const quint32 type = readU32(elf, base);
                    if (type != 1)
                        continue;
                    const quint32 offset = readU32(elf, base + 4);
                    const quint32 filesz = readU32(elf, base + 16);
                    if (offset >= quint32(elf.size()))
                        continue;
                    const int available = int(qMin<quint32>(filesz,
                                                           quint32(elf.size()) - offset));
                    const QByteArray segment = elf.mid(int(offset), available);
                    for (int pos = 0; pos + UnitHeader::kSize <= segment.size(); pos += 4) {
                        UnitHeader candidate;
                        if (parseHeaderAt(segment, pos, &candidate))
                            consider(candidate);
                    }
                }
            }
        }
    }

    // Last resort: scan the entire file for the header signature.
    if (!bestFound) {
        for (int pos = 0; pos + UnitHeader::kSize <= elf.size(); pos += 4) {
            UnitHeader candidate;
            if (parseHeaderAt(elf, pos, &candidate))
                consider(candidate);
        }
    }

    if (bestFound) {
        *header = best;
        return true;
    }
    return false;
}

bool UserUnit::parseHeaderAt(const QByteArray &data, int offset, UnitHeader *header)
{
    if (offset < 0 || offset + 56 > data.size())
        return false;

    UnitHeader candidate;
    candidate.headerSize = readU32(data, offset);
    if (candidate.headerSize != kHeaderMagic)
        return false;
    candidate.target = readU32(data, offset + 4);
    candidate.api = readU32(data, offset + 8);
    candidate.devId = readU32(data, offset + 12);
    candidate.unitId = readU32(data, offset + 16);
    candidate.version = readU32(data, offset + 20);
    candidate.name = readAscii(data, offset + 24, 20);
    candidate.reserved0 = readU32(data, offset + 44);
    candidate.reserved1 = readU32(data, offset + 48);
    candidate.numParams = readU32(data, offset + 52);

    if (!plausibleHeader(candidate))
        return false;

    candidate.valid = true;
    const int paramsOffset = offset + 56;
    for (quint32 i = 0; i < candidate.numParams; ++i) {
        const int base = paramsOffset + int(i) * UnitHeader::kParamSize;
        if (base + UnitHeader::kParamSize > data.size())
            break;
        UnitParam param;
        param.min = readS16(data, base);
        param.max = readS16(data, base + 2);
        param.center = readS16(data, base + 4);
        param.init = readS16(data, base + 6);
        param.type = quint8(data[base + 8]);
        param.flags = quint8(data[base + 9]);
        param.name = readAscii(data, base + 10, 22);
        candidate.params.append(param);
    }

    *header = candidate;
    return true;
}

QString UserUnit::displayName() const
{
    if (!m_header.name.isEmpty()
        && m_header.name.compare(QStringLiteral("undefined"), Qt::CaseInsensitive) != 0)
        return m_header.name;
    return QStringLiteral("Unnamed unit");
}

QString UserUnit::targetDescription() const
{
    if (!m_header.valid)
        return QStringLiteral("Invalid unit");
    return QStringLiteral("%1, API %2").arg(m_header.moduleName(), m_header.apiString());
}

} // namespace korg
