#pragma once

#include "core/model/Workspace.h"

#include <QHash>
#include <QWidget>

class QComboBox;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace korg {

class GlobalPage : public QWidget
{
    Q_OBJECT

public:
    explicit GlobalPage(QWidget *parent = nullptr);

    void setWorkspace(Workspace *workspace);
    void refresh();

signals:
    void receiveRequested();
    void sendRequested();
    void modified();

private:
    void updateControls();
    void updateRawView();
    void applyFieldValue(const QString &key, int rawValue);

    Workspace *m_workspace = nullptr;
    bool m_updating = false;
    QLabel *m_note = nullptr;
    QFormLayout *m_form = nullptr;
    QHash<QString, QSpinBox *> m_spins;
    QHash<QString, QComboBox *> m_combos;
    QPlainTextEdit *m_raw = nullptr;
    QPushButton *m_receive = nullptr;
    QPushButton *m_send = nullptr;
};

} // namespace korg
