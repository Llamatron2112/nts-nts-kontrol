#pragma once

#include "core/model/Program.h"
#include "core/model/Workspace.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace korg {

class ProgramPage : public QWidget
{
    Q_OBJECT

public:
    explicit ProgramPage(QWidget *parent = nullptr);

    void setWorkspace(Workspace *workspace);
    void refresh();

public slots:
    void importProgram();
    void exportProgram();
    void addCurrentToLibrary();

signals:
    void receiveCurrentRequested();
    void sendCurrentRequested();
    void sendProgramRequested(const korg::Program &program);
    void modified();

private:
    void updateCurrent();
    void updateLibrary();
    int selectedSlot() const;
    Program programAt(int slot) const;

    Workspace *m_workspace = nullptr;
    bool m_updating = false;

    QLabel *m_name = nullptr;
    QLabel *m_oscillator = nullptr;
    QLabel *m_modulation = nullptr;
    QLabel *m_delay = nullptr;
    QLabel *m_reverb = nullptr;
    QLabel *m_version = nullptr;
    QPushButton *m_receive = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_import = nullptr;
    QPushButton *m_export = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace korg
