#include "ui/GlobalPage.h"

#include "core/model/GlobalData.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace korg {

GlobalPage::GlobalPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    auto *group = new QGroupBox(QStringLiteral("Global Settings"), this);
    m_form = new QFormLayout(group);
    m_form->setHorizontalSpacing(24);
    m_form->setVerticalSpacing(10);

    for (const GlobalField &field : GlobalData::fields()) {
        const QString key = field.key;
        if (!field.enumNames.isEmpty()) {
            auto *combo = new QComboBox(group);
            for (int raw = field.minimum; raw <= field.maximum; ++raw) {
                const int index = raw - field.minimum;
                const QString name = index < field.enumNames.size()
                                         ? field.enumNames.at(index)
                                         : QString::number(raw);
                combo->addItem(QStringLiteral("%1: %2").arg(raw).arg(name), raw);
            }
            if (!field.help.isEmpty())
                combo->setToolTip(field.help);
            m_form->addRow(field.label, combo);
            m_combos.insert(key, combo);
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    [this, key, field, combo](int index) {
                        if (index < 0)
                            return;
                        applyFieldValue(key, field.minimum + index);
                    });
        } else {
            auto *spin = new QSpinBox(group);
            spin->setRange(field.minimum + field.displayOffset,
                           field.maximum + field.displayOffset);
            spin->setSuffix(field.suffix);
            if (!field.help.isEmpty())
                spin->setToolTip(field.help);
            m_form->addRow(field.label, spin);
            m_spins.insert(key, spin);
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this, key, field](int value) {
                        applyFieldValue(key, value - field.displayOffset);
                    });
        }
    }

    auto *buttonRow = new QHBoxLayout();
    m_receive = new QPushButton(QStringLiteral("Receive"), group);
    m_send = new QPushButton(QStringLiteral("Write"), group);
    buttonRow->addWidget(m_receive);
    buttonRow->addWidget(m_send);
    buttonRow->addStretch(1);
    m_form->addRow(buttonRow);

    layout->addWidget(group);

    auto *rawGroup = new QGroupBox(QStringLiteral("Raw Data"), this);
    auto *rawLayout = new QVBoxLayout(rawGroup);
    m_raw = new QPlainTextEdit(rawGroup);
    m_raw->setReadOnly(true);
    m_raw->setFont(QFont(QStringLiteral("Monospace"), 10));
    m_raw->setMaximumHeight(120);
    rawLayout->addWidget(m_raw);
    layout->addWidget(rawGroup);
    layout->addStretch(1);

    connect(m_receive, &QPushButton::clicked, this, &GlobalPage::receiveRequested);
    connect(m_send, &QPushButton::clicked, this, &GlobalPage::sendRequested);
}

void GlobalPage::setWorkspace(Workspace *workspace)
{
    m_workspace = workspace;
    refresh();
}

void GlobalPage::refresh()
{
    updateControls();
    updateRawView();
}

void GlobalPage::updateControls()
{
    m_updating = true;
    const bool valid = m_workspace && m_workspace->global().isValid();
    m_note->setText(valid
                        ? QStringLiteral("These settings are read from and written to the "
                                         "NTS-1 mkII global data block.")
                        : QStringLiteral("No global data has been received yet. "
                                         "Connect the device and click Receive."));
    m_send->setEnabled(valid);
    for (const GlobalField &field : GlobalData::fields()) {
        if (QComboBox *combo = m_combos.value(field.key)) {
            combo->setEnabled(valid);
            if (valid)
                combo->setCurrentIndex(m_workspace->global().value(field.offset)
                                       - field.minimum);
            continue;
        }
        QSpinBox *spin = m_spins.value(field.key);
        if (!spin)
            continue;
        spin->setEnabled(valid);
        if (valid)
            spin->setValue(m_workspace->global().value(field.offset)
                           + field.displayOffset);
    }
    m_updating = false;
}

void GlobalPage::updateRawView()
{
    if (!m_raw || !m_workspace)
        return;
    if (!m_workspace->global().isValid()) {
        m_raw->setPlainText(QStringLiteral("—"));
        return;
    }
    const QByteArray bytes = m_workspace->global().deviceBytes();
    QString text;
    for (int offset = 0; offset < bytes.size(); offset += 16) {
        text += QStringLiteral("%1  ").arg(offset, 4, 16, QLatin1Char('0'));
        for (int i = 0; i < 16; ++i) {
            if (offset + i < bytes.size())
                text += QStringLiteral("%1 ").arg(quint8(bytes[offset + i]), 2, 16,
                                                 QLatin1Char('0'));
            else
                text += QStringLiteral("   ");
        }
        text += QStringLiteral("  ");
        for (int i = 0; i < 16 && offset + i < bytes.size(); ++i) {
            const quint8 byte = quint8(bytes[offset + i]);
            text += (byte >= 32 && byte < 127) ? QChar(byte) : QLatin1Char('.');
        }
        text += QLatin1Char('\n');
    }
    m_raw->setPlainText(text);
}

void GlobalPage::applyFieldValue(const QString &key, int rawValue)
{
    if (m_updating || !m_workspace || !m_workspace->global().isValid())
        return;
    for (const GlobalField &field : GlobalData::fields()) {
        if (field.key != key)
            continue;
        m_workspace->global().setValue(field.offset, rawValue);
        updateRawView();
        emit modified();
        return;
    }
}

} // namespace korg
