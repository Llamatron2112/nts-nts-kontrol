#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace korg {

struct GlobalField {
    int offset = 0;
    int width = 1;
    QString key;
    QString label;
    int minimum = 0;
    int maximum = 255;
    int displayOffset = 0;
    QStringList enumNames;
    QString suffix;
    QString help;
};

class GlobalData
{
public:
    static constexpr int kDeviceSize = 32;
    static constexpr int kWorkspaceSize = 48;

    GlobalData();

    static GlobalData fromDeviceBytes(const QByteArray &bytes, bool *ok = nullptr);
    static bool looksLikeDeviceData(const QByteArray &bytes);
    static QByteArray defaultDeviceData();
    static QByteArray defaultWorkspaceData();

    bool isValid() const;
    const QByteArray &deviceBytes() const { return m_device; }
    QByteArray &deviceBytes() { return m_device; }
    const QByteArray &workspaceBytes() const { return m_workspace; }
    void setWorkspaceBytes(const QByteArray &bytes);

    int value(int offset) const;
    void setValue(int offset, int value);

    int midiRoute() const;
    bool usbMidiDisabled() const;
    int midiChannel() const;

    static QList<GlobalField> fields();

private:
    QByteArray m_device;
    QByteArray m_workspace;
};

} // namespace korg
