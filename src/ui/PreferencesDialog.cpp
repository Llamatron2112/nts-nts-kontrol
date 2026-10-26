#include "ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace korg {

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Preferences"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);

    auto *heading = new QLabel(QStringLiteral("MIDI and transfer options"), this);
    QFont font = heading->font();
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);

    auto *backendLabel = new QLabel(QStringLiteral("MIDI backend"), this);
    layout->addWidget(backendLabel);
    m_backend = new QComboBox(this);
    m_backend->addItem(QStringLiteral("Auto (PipeWire preferred)"), QStringLiteral("auto"));
    m_backend->addItem(QStringLiteral("PipeWire (JACK MIDI)"), QStringLiteral("pipewire"));
    m_backend->addItem(QStringLiteral("ALSA raw"), QStringLiteral("alsa"));
    if (!midiPipeWireAvailable()) {
        if (auto *model = qobject_cast<QStandardItemModel *>(m_backend->model())) {
            if (QStandardItem *item = model->item(1))
                item->setEnabled(false);
        }
    }
    layout->addWidget(m_backend);

    m_warn = new QCheckBox(QStringLiteral("Warn before writing data to the device"), this);
    m_warn->setChecked(true);
    layout->addWidget(m_warn);

    m_autoReceive = new QCheckBox(
        QStringLiteral("Receive the current program and global data when a device is selected"),
        this);
    m_autoReceive->setChecked(true);
    layout->addWidget(m_autoReceive);

    auto *note = new QLabel(
        QStringLiteral("Auto uses PipeWire's JACK MIDI bridge when available and falls "
                       "back to raw ALSA MIDI. PipeWire normally owns the raw MIDI device, "
                       "so the ALSA raw backend can fail with a busy error while PipeWire "
                       "is running."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel,
                                         this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool PreferencesDialog::warnBeforeWrite() const
{
    return m_warn->isChecked();
}

void PreferencesDialog::setWarnBeforeWrite(bool value)
{
    m_warn->setChecked(value);
}

bool PreferencesDialog::autoReceive() const
{
    return m_autoReceive->isChecked();
}

void PreferencesDialog::setAutoReceive(bool value)
{
    m_autoReceive->setChecked(value);
}

MidiBackendPreference PreferencesDialog::backendPreference() const
{
    return midiBackendPreferenceFromString(m_backend->currentData().toString());
}

void PreferencesDialog::setBackendPreference(MidiBackendPreference preference)
{
    const int index = m_backend->findData(midiBackendPreferenceToString(preference));
    if (index >= 0)
        m_backend->setCurrentIndex(index);
}

} // namespace korg
