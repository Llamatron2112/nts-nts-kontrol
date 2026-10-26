#include "ui/UnitPage.h"

#include "core/file/WorkspaceFile.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace korg {

namespace {

QTableWidgetItem *item(const QString &text, bool editable = false)
{
    auto *entry = new QTableWidgetItem(text);
    entry->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled
                    | (editable ? Qt::ItemIsEditable : Qt::ItemFlags()));
    return entry;
}

} // namespace

UnitPage::UnitPage(Module module, QWidget *parent)
    : QWidget(parent)
    , m_module(module)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    m_summary = new QLabel(this);
    QFont summaryFont = m_summary->font();
    summaryFont.setPointSize(summaryFont.pointSize() + 2);
    m_summary->setFont(summaryFont);
    layout->addWidget(m_summary);

    auto *buttonRow = new QHBoxLayout();
    m_import = new QPushButton(QStringLiteral("Import…"), this);
    m_export = new QPushButton(QStringLiteral("Export…"), this);
    m_clear = new QPushButton(QStringLiteral("Clear"), this);
    m_receive = new QPushButton(QStringLiteral("Receive"), this);
    m_sendAll = new QPushButton(QStringLiteral("Send All"), this);
    m_sendSelected = new QPushButton(QStringLiteral("Send Selected"), this);
    m_moveUp = new QPushButton(QStringLiteral("Move Up"), this);
    m_moveDown = new QPushButton(QStringLiteral("Move Down"), this);
    buttonRow->addWidget(m_import);
    buttonRow->addWidget(m_export);
    buttonRow->addWidget(m_clear);
    buttonRow->addSpacing(12);
    buttonRow->addWidget(m_receive);
    buttonRow->addWidget(m_sendAll);
    buttonRow->addWidget(m_sendSelected);
    buttonRow->addSpacing(12);
    buttonRow->addWidget(m_moveUp);
    buttonRow->addWidget(m_moveDown);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    m_table = new QTableWidget(slotCount(m_module), 7, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Slot"), QStringLiteral("Name"),
                                        QStringLiteral("Device ID"),
                                        QStringLiteral("Unit ID"),
                                        QStringLiteral("API"),
                                        QStringLiteral("Version"),
                                        QStringLiteral("Params")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column = 2; column < 7; ++column)
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    connect(m_import, &QPushButton::clicked, this, &UnitPage::importUnit);
    connect(m_export, &QPushButton::clicked, this, &UnitPage::exportUnit);
    connect(m_clear, &QPushButton::clicked, this, &UnitPage::clearUnit);
    connect(m_receive, &QPushButton::clicked, this, [this] { emit receiveRequested(m_module); });
    connect(m_sendAll, &QPushButton::clicked, this, [this] { emit sendRequested(m_module); });
    connect(m_sendSelected, &QPushButton::clicked, this, &UnitPage::sendSelected);
    connect(m_moveUp, &QPushButton::clicked, this, &UnitPage::moveUp);
    connect(m_moveDown, &QPushButton::clicked, this, &UnitPage::moveDown);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        sendSelected();
    });
}

void UnitPage::setWorkspace(Workspace *workspace)
{
    m_workspace = workspace;
    refresh();
}

UserUnit UnitPage::unitAt(int slot) const
{
    if (!m_workspace || slot < 0)
        return {};
    const QVector<UserUnit> &units = m_workspace->units().forModule(m_module);
    if (slot >= units.size())
        return {};
    return units.at(slot);
}

int UnitPage::selectedSlot() const
{
    return m_table ? m_table->currentRow() : -1;
}

void UnitPage::refresh()
{
    if (!m_table)
        return;
    const int rowCount = slotCount(m_module);
    m_table->setRowCount(rowCount);
    int used = 0;
    for (int row = 0; row < rowCount; ++row) {
        const UserUnit unit = unitAt(row);
        const bool present = !unit.isEmpty();
        if (present)
            ++used;
        if (!m_table->item(row, 0)) {
            for (int column = 0; column < 7; ++column)
                m_table->setItem(row, column, item(QString()));
            m_table->item(row, 0)->setText(QString::number(row));
        }
        m_table->item(row, 1)->setText(present ? unit.displayName() : QStringLiteral("—"));
        if (present && unit.isValid()) {
            const UnitHeader &header = unit.header();
            m_table->item(row, 2)->setText(header.devIdString());
            m_table->item(row, 3)->setText(header.unitIdString());
            m_table->item(row, 4)->setText(header.apiString());
            m_table->item(row, 5)->setText(header.versionString());
            m_table->item(row, 6)->setText(QString::number(header.numParams));
        } else {
            for (int column = 2; column < 7; ++column)
                m_table->item(row, column)->setText(
                    present ? QStringLiteral("?") : QStringLiteral("—"));
        }
    }
    m_summary->setText(QStringLiteral("%1 — %2 / %3 slots used")
                           .arg(moduleDisplayName(m_module))
                           .arg(used)
                           .arg(rowCount));
}

void UnitPage::importUnit()
{
    if (!m_workspace)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import User Unit"), QString(),
        QStringLiteral("NTS-1 mkII user unit (*.nts1mkiiunit);;All files (*)"));
    if (path.isEmpty())
        return;

    UserUnit unit;
    QString error;
    if (!WorkspaceFile::readUnitFile(path, &unit, &error)) {
        QMessageBox::critical(this, QStringLiteral("Import User Unit"), error);
        return;
    }
    if (unit.isValid() && unit.header().hasModule()
        && unit.header().module() != m_module) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Import User Unit"),
            QStringLiteral("This unit targets %1, but the current tab is %2.\n"
                           "Import it anyway?")
                .arg(unit.header().moduleName(), moduleDisplayName(m_module)));
        if (answer != QMessageBox::Yes)
            return;
    }

    int target = selectedSlot();
    QVector<UserUnit> &units = m_workspace->units().forModule(m_module);
    if (target < 0 || (!units.at(target).isEmpty()
                       && QMessageBox::question(
                              this, QStringLiteral("Import User Unit"),
                              QStringLiteral("Overwrite the selected slot?"))
                              != QMessageBox::Yes)) {
        target = -1;
        for (int i = 0; i < units.size(); ++i) {
            if (units.at(i).isEmpty()) {
                target = i;
                break;
            }
        }
    }
    if (target < 0) {
        QMessageBox::information(this, QStringLiteral("Import User Unit"),
                                 QStringLiteral("No free slot is available."));
        return;
    }
    units[target] = unit;
    emit modified();
    refresh();
    m_table->selectRow(target);
}

void UnitPage::exportUnit()
{
    const int slot = selectedSlot();
    const UserUnit unit = unitAt(slot);
    if (slot < 0 || unit.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export User Unit"),
                                 QStringLiteral("Select a loaded user unit."));
        return;
    }
    QString suggested = unit.displayName();
    if (suggested.isEmpty() || suggested == QStringLiteral("Unnamed unit"))
        suggested = QStringLiteral("unit");
    suggested += QStringLiteral(".nts1mkiiunit");
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export User Unit"), suggested,
        QStringLiteral("NTS-1 mkII user unit (*.nts1mkiiunit);;All files (*)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!WorkspaceFile::writeUnitFile(path, unit, &error))
        QMessageBox::critical(this, QStringLiteral("Export User Unit"), error);
}

void UnitPage::clearUnit()
{
    if (!m_workspace)
        return;
    const int slot = selectedSlot();
    if (slot < 0) {
        QMessageBox::information(this, QStringLiteral("Clear User Unit"),
                                 QStringLiteral("Select a slot."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Clear User Unit"),
                              QStringLiteral("Clear slot %1 from the local workspace?\n"
                                             "The device is not changed until you send.")
                                  .arg(slot))
        != QMessageBox::Yes)
        return;
    m_workspace->units().forModule(m_module)[slot] = UserUnit();
    emit modified();
    refresh();
}

void UnitPage::moveUp()
{
    if (!m_workspace)
        return;
    const int slot = selectedSlot();
    if (slot <= 0)
        return;
    QVector<UserUnit> &units = m_workspace->units().forModule(m_module);
    std::swap(units[slot], units[slot - 1]);
    emit modified();
    refresh();
    m_table->selectRow(slot - 1);
}

void UnitPage::moveDown()
{
    if (!m_workspace)
        return;
    const int slot = selectedSlot();
    if (slot < 0 || slot + 1 >= slotCount(m_module))
        return;
    QVector<UserUnit> &units = m_workspace->units().forModule(m_module);
    std::swap(units[slot], units[slot + 1]);
    emit modified();
    refresh();
    m_table->selectRow(slot + 1);
}

void UnitPage::sendSelected()
{
    const int slot = selectedSlot();
    if (slot < 0 || unitAt(slot).isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Send User Unit"),
                                 QStringLiteral("Select a loaded user unit."));
        return;
    }
    emit sendSlotRequested(m_module, slot);
}

} // namespace korg
