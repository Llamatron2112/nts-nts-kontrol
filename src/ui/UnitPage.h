#pragma once

#include "core/model/Workspace.h"
#include "core/protocol/KorgProtocol.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace korg {

class UnitPage : public QWidget
{
    Q_OBJECT

public:
    explicit UnitPage(Module module, QWidget *parent = nullptr);

    void setWorkspace(Workspace *workspace);
    void refresh();

    Module module() const { return m_module; }

public slots:
    void importUnit();
    void exportUnit();
    void clearUnit();
    void moveUp();
    void moveDown();
    void sendSelected();

signals:
    void receiveRequested(korg::Module module);
    void sendRequested(korg::Module module);
    void sendSlotRequested(korg::Module module, int slot);
    void modified();

private:
    UserUnit unitAt(int slot) const;
    int selectedSlot() const;

    Module m_module;
    Workspace *m_workspace = nullptr;
    QLabel *m_summary = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_import = nullptr;
    QPushButton *m_export = nullptr;
    QPushButton *m_clear = nullptr;
    QPushButton *m_receive = nullptr;
    QPushButton *m_sendAll = nullptr;
    QPushButton *m_sendSelected = nullptr;
    QPushButton *m_moveUp = nullptr;
    QPushButton *m_moveDown = nullptr;
};

} // namespace korg
