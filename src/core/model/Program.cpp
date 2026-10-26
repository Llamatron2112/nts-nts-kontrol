#include "core/model/Program.h"

#include <QStringDecoder>

#include <algorithm>

namespace korg {

namespace {

QString asciiText(const QByteArray &bytes, int offset, int length)
{
    if (offset < 0 || length <= 0 || offset + length > bytes.size())
        return {};
    QByteArray slice = bytes.mid(offset, length);
    const int nul = slice.indexOf('\0');
    if (nul >= 0)
        slice.truncate(nul);
    return QString::fromLatin1(slice).trimmed();
}

void writeAscii(QByteArray &bytes, int offset, int length, const QString &text)
{
    if (offset < 0 || length <= 0 || offset + length > bytes.size())
        return;
    QByteArray encoded = text.toLatin1();
    if (encoded.size() > length - 1)
        encoded.truncate(length - 1);
    std::fill(bytes.begin() + offset, bytes.begin() + offset + length, '\0');
    std::copy(encoded.begin(), encoded.end(), bytes.begin() + offset);
}

} // namespace

Program::Program(const QByteArray &bytes)
    : m_data(bytes)
{
}

Program Program::fromBytes(const QByteArray &bytes, bool *ok)
{
    Program program;
    if (looksValid(bytes))
        program.m_data = bytes;
    if (ok)
        *ok = program.isValid();
    return program;
}

bool Program::looksValid(const QByteArray &bytes)
{
    if (bytes.size() < kSize)
        return false;
    return bytes.mid(0, 4) == QByteArrayLiteral("PROG")
           && bytes.mid(kSize - 4, 4) == QByteArrayLiteral("PRED");
}

bool Program::isValid() const
{
    return looksValid(m_data);
}

QString Program::readText(int offset, int length) const
{
    return asciiText(m_data, offset, length);
}

QString Program::name() const
{
    return readText(kNameOffset, kNameLength);
}

void Program::setName(const QString &value)
{
    if (m_data.size() != kSize)
        return;
    writeAscii(m_data, kNameOffset, kNameLength, value);
}

QString Program::oscillatorName() const
{
    return readText(kOscNameOffset, kPartNameLength);
}

QString Program::modulationName() const
{
    return readText(kModNameOffset, kPartNameLength);
}

QString Program::delayName() const
{
    return readText(kDelayNameOffset, kPartNameLength);
}

QString Program::reverbName() const
{
    return readText(kReverbNameOffset, kPartNameLength);
}

QString Program::versionString() const
{
    if (m_data.size() < 8)
        return {};
    const int major = quint8(m_data[4]) | (quint8(m_data[5]) << 8);
    const int minor = quint8(m_data[6]);
    const int patch = quint8(m_data[7]);
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString Program::displayName() const
{
    const QString value = name();
    if (!value.isEmpty())
        return value;
    return QStringLiteral("Untitled");
}

} // namespace korg
