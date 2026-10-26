#pragma once

#include <QByteArray>
#include <QString>

namespace korg {

class Program
{
public:
    static constexpr int kSize = 504;
    static constexpr int kNameOffset = 12;
    static constexpr int kNameLength = 16;
    static constexpr int kOscNameOffset = 40;
    static constexpr int kModNameOffset = 124;
    static constexpr int kDelayNameOffset = 180;
    static constexpr int kReverbNameOffset = 236;
    static constexpr int kPartNameLength = 20;

    Program() = default;
    explicit Program(const QByteArray &bytes);

    static Program fromBytes(const QByteArray &bytes, bool *ok = nullptr);
    static bool looksValid(const QByteArray &bytes);

    bool isValid() const;
    bool isEmpty() const { return m_data.isEmpty(); }
    const QByteArray &bytes() const { return m_data; }
    QByteArray &bytes() { return m_data; }

    QString name() const;
    void setName(const QString &name);
    QString oscillatorName() const;
    QString modulationName() const;
    QString delayName() const;
    QString reverbName() const;
    QString versionString() const;
    QString displayName() const;

private:
    QString readText(int offset, int length) const;

    QByteArray m_data;
};

} // namespace korg
