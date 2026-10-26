#include "ui/ProgramPage.h"

#include "core/file/WorkspaceFile.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace korg {

namespace {
QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    return item;
}
} // namespace

ProgramPage::ProgramPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto *currentBox = new QGroupBox(QStringLiteral("Current Program"), this);
    auto *currentLayout = new QGridLayout(currentBox);
    currentLayout->setHorizontalSpacing(24);
    currentLayout->setVerticalSpacing(8);

    auto *nameCaption = new QLabel(QStringLiteral("Name"), currentBox);
    auto *oscCaption = new QLabel(QStringLiteral("Oscillator"), currentBox);
    auto *modCaption = new QLabel(QStringLiteral("Modulation"), currentBox);
    auto *delayCaption = new QLabel(QStringLiteral("Delay"), currentBox);
    auto *reverbCaption = new QLabel(QStringLiteral("Reverb"), currentBox);
    auto *versionCaption = new QLabel(QStringLiteral("Version"), currentBox);

    for (QLabel *label : {nameCaption, oscCaption, modCaption, delayCaption,
                          reverbCaption, versionCaption}) {
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
    }

    m_name = new QLabel(QStringLiteral("Not received"), currentBox);
    m_oscillator = new QLabel(QStringLiteral("—"), currentBox);
    m_modulation = new QLabel(QStringLiteral("—"), currentBox);
    m_delay = new QLabel(QStringLiteral("—"), currentBox);
    m_reverb = new QLabel(QStringLiteral("—"), currentBox);
    m_version = new QLabel(QStringLiteral("—"), currentBox);

    currentLayout->addWidget(nameCaption, 0, 0);
    currentLayout->addWidget(m_name, 0, 1);
    currentLayout->addWidget(oscCaption, 0, 2);
    currentLayout->addWidget(m_oscillator, 0, 3);
    currentLayout->addWidget(modCaption, 0, 4);
    currentLayout->addWidget(m_modulation, 0, 5);
    currentLayout->addWidget(delayCaption, 1, 0);
    currentLayout->addWidget(m_delay, 1, 1);
    currentLayout->addWidget(reverbCaption, 1, 2);
    currentLayout->addWidget(m_reverb, 1, 3);
    currentLayout->addWidget(versionCaption, 1, 4);
    currentLayout->addWidget(m_version, 1, 5);
    currentLayout->setColumnStretch(1, 1);
    currentLayout->setColumnStretch(3, 1);
    currentLayout->setColumnStretch(5, 1);

    auto *buttonRow = new QHBoxLayout();
    m_receive = new QPushButton(QStringLiteral("Receive"), currentBox);
    m_send = new QPushButton(QStringLiteral("Send"), currentBox);
    m_add = new QPushButton(QStringLiteral("Add Current to Library"), currentBox);
    buttonRow->addWidget(m_receive);
    buttonRow->addWidget(m_send);
    buttonRow->addWidget(m_add);
    buttonRow->addStretch(1);
    currentLayout->addLayout(buttonRow, 2, 0, 1, 6);

    layout->addWidget(currentBox);

    auto *libraryHeader = new QHBoxLayout();
    auto *libraryTitle = new QLabel(QStringLiteral("Local Library"), this);
    QFont titleFont = libraryTitle->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    libraryTitle->setFont(titleFont);
    libraryHeader->addWidget(libraryTitle);
    libraryHeader->addStretch(1);
    m_import = new QPushButton(QStringLiteral("Import…"), this);
    m_export = new QPushButton(QStringLiteral("Export…"), this);
    libraryHeader->addWidget(m_import);
    libraryHeader->addWidget(m_export);
    layout->addLayout(libraryHeader);

    m_table = new QTableWidget(Workspace::kLibrarySlots, 6, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("No."), QStringLiteral("Name"),
                                        QStringLiteral("Oscillator"),
                                        QStringLiteral("Modulation"),
                                        QStringLiteral("Delay"),
                                        QStringLiteral("Reverb")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::EditKeyPressed);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column = 2; column < 5; ++column)
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    connect(m_receive, &QPushButton::clicked, this, &ProgramPage::receiveCurrentRequested);
    connect(m_send, &QPushButton::clicked, this, &ProgramPage::sendCurrentRequested);
    connect(m_add, &QPushButton::clicked, this, &ProgramPage::addCurrentToLibrary);
    connect(m_import, &QPushButton::clicked, this, &ProgramPage::importProgram);
    connect(m_export, &QPushButton::clicked, this, &ProgramPage::exportProgram);
    connect(m_table, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *item) {
                if (m_updating || !m_workspace || !item || item->column() != 1)
                    return;
                const int row = item->row();
                if (row < 0 || row >= m_workspace->library().size())
                    return;
                Program program = m_workspace->library().at(row);
                if (!program.isValid())
                    return;
                QString text = item->text().trimmed();
                if (text == QStringLiteral("Untitled"))
                    text.clear();
                program.setName(text);
                m_workspace->library()[row] = program;
                emit modified();
            });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const Program program = programAt(row);
        if (program.isValid())
            emit sendProgramRequested(program);
    });
}

void ProgramPage::setWorkspace(Workspace *workspace)
{
    m_workspace = workspace;
    refresh();
}

void ProgramPage::refresh()
{
    updateCurrent();
    updateLibrary();
}

void ProgramPage::updateCurrent()
{
    const bool valid = m_workspace && m_workspace->currentProgram().isValid();
    if (!valid) {
        m_name->setText(QStringLiteral("Not received"));
        m_oscillator->setText(QStringLiteral("—"));
        m_modulation->setText(QStringLiteral("—"));
        m_delay->setText(QStringLiteral("—"));
        m_reverb->setText(QStringLiteral("—"));
        m_version->setText(QStringLiteral("—"));
        return;
    }
    const Program &program = m_workspace->currentProgram();
    m_name->setText(program.displayName());
    m_oscillator->setText(program.oscillatorName().isEmpty()
                              ? QStringLiteral("—")
                              : program.oscillatorName());
    m_modulation->setText(program.modulationName().isEmpty()
                              ? QStringLiteral("—")
                              : program.modulationName());
    m_delay->setText(program.delayName().isEmpty() ? QStringLiteral("—")
                                                   : program.delayName());
    m_reverb->setText(program.reverbName().isEmpty() ? QStringLiteral("—")
                                                     : program.reverbName());
    m_version->setText(program.versionString());
}

int ProgramPage::selectedSlot() const
{
    return m_table ? m_table->currentRow() : -1;
}

Program ProgramPage::programAt(int slot) const
{
    if (!m_workspace || slot < 0 || slot >= m_workspace->library().size())
        return {};
    return m_workspace->library().at(slot);
}

void ProgramPage::updateLibrary()
{
    if (!m_table)
        return;
    m_updating = true;
    const int currentRow = m_table->currentRow();
    m_table->setRowCount(Workspace::kLibrarySlots);
    for (int row = 0; row < Workspace::kLibrarySlots; ++row) {
        const Program program = programAt(row);
        const bool valid = program.isValid();
        if (!m_table->item(row, 0)) {
            m_table->setItem(row, 0, readOnlyItem(QString::number(row + 1)));
            for (int column = 2; column < 6; ++column)
                m_table->setItem(row, column, readOnlyItem(QStringLiteral("—")));
        }
        auto *nameItem = m_table->item(row, 1);
        if (!nameItem) {
            nameItem = new QTableWidgetItem();
            m_table->setItem(row, 1, nameItem);
        }
        nameItem->setText(valid ? program.displayName() : QStringLiteral("—"));
        nameItem->setFlags(valid
                               ? (Qt::ItemIsSelectable | Qt::ItemIsEnabled
                                  | Qt::ItemIsEditable)
                               : (Qt::ItemIsSelectable | Qt::ItemIsEnabled));
        auto setText = [this, row](int column, const QString &text) {
            QTableWidgetItem *item = m_table->item(row, column);
            if (item)
                item->setText(text);
        };
        setText(2, valid && !program.oscillatorName().isEmpty()
                     ? program.oscillatorName()
                     : QStringLiteral("—"));
        setText(3, valid && !program.modulationName().isEmpty()
                     ? program.modulationName()
                     : QStringLiteral("—"));
        setText(4, valid && !program.delayName().isEmpty() ? program.delayName()
                                                           : QStringLiteral("—"));
        setText(5, valid && !program.reverbName().isEmpty() ? program.reverbName()
                                                            : QStringLiteral("—"));
    }
    if (currentRow >= 0 && currentRow < m_table->rowCount())
        m_table->selectRow(currentRow);
    m_updating = false;
}

void ProgramPage::importProgram()
{
    if (!m_workspace)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import Program"), QString(),
        QStringLiteral("NTS-1 mkII program (*.nts1mkiiprog);;All files (*)"));
    if (path.isEmpty())
        return;

    Program program;
    QString error;
    if (!WorkspaceFile::readProgramFile(path, &program, &error)) {
        QMessageBox::critical(this, QStringLiteral("Import Program"), error);
        return;
    }

    int target = selectedSlot();
    if (target < 0 || (m_workspace->library().at(target).isValid()
                       && QMessageBox::question(
                              this, QStringLiteral("Import Program"),
                              QStringLiteral("Overwrite the selected library slot?"))
                              != QMessageBox::Yes))
        target = m_workspace->firstFreeLibrarySlot();
    if (target < 0) {
        QMessageBox::information(this, QStringLiteral("Import Program"),
                                 QStringLiteral("The local library is full."));
        return;
    }
    m_workspace->library()[target] = program;
    emit modified();
    refresh();
    m_table->selectRow(target);
}

void ProgramPage::exportProgram()
{
    if (!m_workspace)
        return;
    const int slot = selectedSlot();
    if (slot < 0 || !programAt(slot).isValid()) {
        QMessageBox::information(this, QStringLiteral("Export Program"),
                                 QStringLiteral("Select a program in the local library."));
        return;
    }
    const Program program = programAt(slot);
    QString suggested = program.displayName();
    if (suggested.isEmpty() || suggested == QStringLiteral("Untitled"))
        suggested = QStringLiteral("program");
    suggested += QStringLiteral(".nts1mkiiprog");
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Program"), suggested,
        QStringLiteral("NTS-1 mkII program (*.nts1mkiiprog);;All files (*)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!WorkspaceFile::writeProgramFile(path, program, &error))
        QMessageBox::critical(this, QStringLiteral("Export Program"), error);
}

void ProgramPage::addCurrentToLibrary()
{
    if (!m_workspace || !m_workspace->currentProgram().isValid()) {
        QMessageBox::information(this, QStringLiteral("Local Library"),
                                 QStringLiteral("Receive a program from the device first."));
        return;
    }
    const int slot = m_workspace->firstFreeLibrarySlot();
    if (slot < 0) {
        QMessageBox::information(this, QStringLiteral("Local Library"),
                                 QStringLiteral("The local library is full."));
        return;
    }
    m_workspace->library()[slot] = m_workspace->currentProgram();
    emit modified();
    refresh();
    m_table->selectRow(slot);
}

} // namespace korg
