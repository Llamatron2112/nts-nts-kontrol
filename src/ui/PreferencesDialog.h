#pragma once

#include "core/midi/MidiBackend.h"

#include <QDialog>

class QCheckBox;
class QComboBox;

namespace korg {

class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    bool warnBeforeWrite() const;
    void setWarnBeforeWrite(bool value);
    bool autoReceive() const;
    void setAutoReceive(bool value);
    MidiBackendPreference backendPreference() const;
    void setBackendPreference(MidiBackendPreference preference);

private:
    QCheckBox *m_warn = nullptr;
    QCheckBox *m_autoReceive = nullptr;
    QComboBox *m_backend = nullptr;
};

} // namespace korg
