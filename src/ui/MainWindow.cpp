#include "ui/MainWindow.h"

#include "core/file/WorkspaceFile.h"
#include "core/model/GlobalData.h"
#include "ui/GlobalPage.h"
#include "ui/PreferencesDialog.h"
#include "ui/ProgramPage.h"
#include "ui/UnitPage.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <memory>

namespace korg {

namespace {

struct TaskResult {
    bool ok = false;
    QString error;
};

QString deviceLabel(const DeviceInfo &device)
{
    return QStringLiteral("%1  ·  MIDI ch %2  ·  SDK %3")
        .arg(device.displayName())
        .arg(device.channel + 1)
        .arg(device.versionString());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("nts-nts-kontrol"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/nts-nts-kontrol.svg")));
    resize(1120, 860);
    buildUi();
    buildMenus();
    loadSettings();

    m_programPage->setWorkspace(&m_workspace);
    m_oscPage->setWorkspace(&m_workspace);
    m_modPage->setWorkspace(&m_workspace);
    m_delPage->setWorkspace(&m_workspace);
    m_revPage->setWorkspace(&m_workspace);
    m_globalPage->setWorkspace(&m_workspace);

    refreshAll();
    updateTitle();
    QTimer::singleShot(250, this, &MainWindow::refreshDevices);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(8);

    auto *deviceBar = new QHBoxLayout();
    auto *deviceLabel = new QLabel(QStringLiteral("Device:"), central);
    m_deviceCombo = new QComboBox(central);
    m_deviceCombo->setMinimumWidth(360);
    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), central);
    m_deviceStatus = new QLabel(QStringLiteral("No device"), central);
    m_deviceStatus->setStyleSheet(QStringLiteral("color: #888888;"));
    deviceBar->addWidget(deviceLabel);
    deviceBar->addWidget(m_deviceCombo, 1);
    deviceBar->addWidget(m_refreshButton);
    deviceBar->addWidget(m_deviceStatus, 1);
    layout->addLayout(deviceBar);

    m_tabs = new QTabWidget(central);
    m_programPage = new ProgramPage(m_tabs);
    m_oscPage = new UnitPage(Module::Osc, m_tabs);
    m_modPage = new UnitPage(Module::ModFx, m_tabs);
    m_delPage = new UnitPage(Module::DelFx, m_tabs);
    m_revPage = new UnitPage(Module::RevFx, m_tabs);
    m_globalPage = new GlobalPage(m_tabs);
    m_tabs->addTab(m_programPage, QStringLiteral("PROGRAM"));
    m_tabs->addTab(m_oscPage, QStringLiteral("OSCILLATORS"));
    m_tabs->addTab(m_modPage, QStringLiteral("MODULATION EFFECTS"));
    m_tabs->addTab(m_delPage, QStringLiteral("DELAY EFFECTS"));
    m_tabs->addTab(m_revPage, QStringLiteral("REVERB EFFECTS"));
    auto *globalScroll = new QScrollArea(m_tabs);
    globalScroll->setWidgetResizable(true);
    globalScroll->setFrameShape(QFrame::NoFrame);
    globalScroll->setWidget(m_globalPage);
    m_tabs->addTab(globalScroll, QStringLiteral("GLOBAL"));
    layout->addWidget(m_tabs, 1);

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Ready"));

    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshDevices);
    connect(m_deviceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onDeviceSelected);

    connect(m_programPage, &ProgramPage::receiveCurrentRequested, this,
            &MainWindow::receiveCurrentProgram);
    connect(m_programPage, &ProgramPage::sendCurrentRequested, this,
            &MainWindow::sendCurrentProgram);
    connect(m_programPage, &ProgramPage::sendProgramRequested, this,
            &MainWindow::sendProgramToDevice);
    connect(m_programPage, &ProgramPage::modified, this, &MainWindow::markModified);

    for (UnitPage *page : {m_oscPage, m_modPage, m_delPage, m_revPage}) {
        connect(page, &UnitPage::receiveRequested, this, &MainWindow::receiveUnitModule);
        connect(page, &UnitPage::sendRequested, this, &MainWindow::sendUnitModule);
        connect(page, &UnitPage::sendSlotRequested, this, &MainWindow::sendUnitSlot);
        connect(page, &UnitPage::modified, this, &MainWindow::markModified);
    }

    connect(m_globalPage, &GlobalPage::receiveRequested, this, &MainWindow::receiveGlobal);
    connect(m_globalPage, &GlobalPage::sendRequested, this, &MainWindow::sendGlobal);
    connect(m_globalPage, &GlobalPage::modified, this, &MainWindow::markModified);
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QAction *newAction = fileMenu->addAction(QStringLiteral("&New"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newWorkspace);
    QAction *openAction = fileMenu->addAction(QStringLiteral("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openWorkspace);
    QAction *saveAction = fileMenu->addAction(QStringLiteral("&Save"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveWorkspace);
    QAction *saveAsAction = fileMenu->addAction(QStringLiteral("Save &As…"));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveWorkspaceAs);
    fileMenu->addSeparator();
    QAction *importProgram = fileMenu->addAction(QStringLiteral("Import Program…"));
    connect(importProgram, &QAction::triggered, m_programPage, &ProgramPage::importProgram);
    QAction *exportProgram = fileMenu->addAction(QStringLiteral("Export Program…"));
    connect(exportProgram, &QAction::triggered, m_programPage, &ProgramPage::exportProgram);
    QAction *importUnit = fileMenu->addAction(QStringLiteral("Import User Unit…"));
    connect(importUnit, &QAction::triggered, this,
            [this] { if (auto *page = defaultUnitPage()) page->importUnit(); });
    QAction *exportUnit = fileMenu->addAction(QStringLiteral("Export User Unit…"));
    connect(exportUnit, &QAction::triggered, this,
            [this] { if (auto *page = defaultUnitPage()) page->exportUnit(); });
    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    QAction *preferencesAction = editMenu->addAction(QStringLiteral("&Preferences…"));
    preferencesAction->setShortcut(QKeySequence::Preferences);
    connect(preferencesAction, &QAction::triggered, this, &MainWindow::showPreferences);

    auto *commMenu = menuBar()->addMenu(QStringLiteral("&Communication"));
    m_receiveAllAction = commMenu->addAction(QStringLiteral("Receive All Data"));
    connect(m_receiveAllAction, &QAction::triggered, this, &MainWindow::receiveAllData);
    m_sendAllAction = commMenu->addAction(QStringLiteral("Send All Data"));
    connect(m_sendAllAction, &QAction::triggered, this, &MainWindow::sendAllData);
    commMenu->addSeparator();
    QAction *rxProgram = commMenu->addAction(QStringLiteral("Receive Current Program"));
    connect(rxProgram, &QAction::triggered, this, &MainWindow::receiveCurrentProgram);
    QAction *txProgram = commMenu->addAction(QStringLiteral("Send Current Program"));
    connect(txProgram, &QAction::triggered, this, &MainWindow::sendCurrentProgram);
    commMenu->addSeparator();
    QAction *rxUnit = commMenu->addAction(QStringLiteral("Receive User Unit"));
    connect(rxUnit, &QAction::triggered, this,
            [this] { if (auto *page = defaultUnitPage()) receiveUnitModule(page->module()); });
    QAction *txUnit = commMenu->addAction(QStringLiteral("Send User Unit"));
    connect(txUnit, &QAction::triggered, this,
            [this] { if (auto *page = defaultUnitPage()) sendUnitModule(page->module()); });
    commMenu->addSeparator();
    QAction *rxGlobal = commMenu->addAction(QStringLiteral("Receive Global Data"));
    connect(rxGlobal, &QAction::triggered, this, &MainWindow::receiveGlobal);
    QAction *txGlobal = commMenu->addAction(QStringLiteral("Write Global Data"));
    connect(txGlobal, &QAction::triggered, this, &MainWindow::sendGlobal);

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction *aboutAction = helpMenu->addAction(QStringLiteral("&About"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::loadSettings()
{
    QSettings settings;
    m_warnBeforeWrite = settings.value(QStringLiteral("preferences/warnBeforeWrite"), true).toBool();
    m_autoReceive = settings.value(QStringLiteral("preferences/autoReceive"), true).toBool();
    m_backendPreference = midiBackendPreferenceFromString(
        settings.value(QStringLiteral("preferences/midiBackend"), QStringLiteral("auto")).toString());
    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("preferences/warnBeforeWrite"), m_warnBeforeWrite);
    settings.setValue(QStringLiteral("preferences/autoReceive"), m_autoReceive);
    settings.setValue(QStringLiteral("preferences/midiBackend"),
                      midiBackendPreferenceToString(m_backendPreference));
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
}

void MainWindow::refreshDevices()
{
    if (m_scanning || m_busy)
        return;
    m_scanning = true;
    m_refreshButton->setEnabled(false);
    m_deviceCombo->setEnabled(false);
    m_deviceStatus->setText(QStringLiteral("Scanning for NTS-1 mkII…"));
    m_deviceStatus->setStyleSheet(QStringLiteral("color: #888888;"));

    auto *watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this, [this, watcher] {
        ScanResult result = watcher->result();
        watcher->deleteLater();
        m_scanning = false;
        m_refreshButton->setEnabled(true);
        m_deviceCombo->setEnabled(true);
        m_devices = result.devices;

        m_deviceCombo->blockSignals(true);
        m_deviceCombo->clear();
        for (const DeviceInfo &device : m_devices)
            m_deviceCombo->addItem(deviceLabel(device));
        m_deviceCombo->blockSignals(false);

        if (m_devices.isEmpty()) {
            m_selectedDevice = DeviceInfo {};
            m_deviceCombo->setCurrentIndex(-1);
            updateDeviceStatus();
            statusBar()->showMessage(result.error, 6000);
        } else {
            m_deviceCombo->setCurrentIndex(0);
            onDeviceSelected(0);
        }
    });

    const MidiBackendPreference preference = m_backendPreference;
    watcher->setFuture(QtConcurrent::run([preference] {
        ScanResult result;
        result.devices = DeviceSession::scan(preference, &result.error);
        return result;
    }));
}

void MainWindow::onDeviceSelected(int index)
{
    if (index < 0 || index >= m_devices.size()) {
        m_selectedDevice = DeviceInfo {};
        updateDeviceStatus();
        return;
    }
    m_selectedDevice = m_devices.at(index);
    updateDeviceStatus();
    if (m_autoReceive && !m_busy)
        QTimer::singleShot(120, this, &MainWindow::autoReceiveStartupData);
}

void MainWindow::updateDeviceStatus()
{
    if (!m_selectedDevice.valid) {
        m_deviceStatus->setText(QStringLiteral("No NTS-1 mkII detected"));
        m_deviceStatus->setStyleSheet(QStringLiteral("color: #cc6666;"));
        return;
    }
    m_deviceStatus->setText(QStringLiteral("Connected: %1 (%2) · MIDI ch %3 · SDK %4")
                                .arg(m_selectedDevice.port.backendName(),
                                     m_selectedDevice.port.hwName())
                                .arg(m_selectedDevice.channel + 1)
                                .arg(m_selectedDevice.versionString()));
    m_deviceStatus->setStyleSheet(QStringLiteral("color: #7bbf6a;"));
}

void MainWindow::updateDeviceChannel(int channel)
{
    if (!m_selectedDevice.valid || channel < 0 || channel > 15)
        return;

    const int index = m_deviceCombo->currentIndex();
    if (index >= 0 && index < m_devices.size()) {
        m_devices[index].channel = channel;
        m_deviceCombo->blockSignals(true);
        m_deviceCombo->setItemText(index, deviceLabel(m_devices.at(index)));
        m_deviceCombo->blockSignals(false);
    }
    m_selectedDevice.channel = channel;
    updateDeviceStatus();
}

void MainWindow::markTransportLost()
{
    m_devices.clear();
    m_deviceCombo->blockSignals(true);
    m_deviceCombo->clear();
    m_deviceCombo->blockSignals(false);
    m_deviceCombo->setCurrentIndex(-1);
    m_selectedDevice = DeviceInfo {};
    updateDeviceStatus();
    statusBar()->showMessage(
        QStringLiteral("Global data written. USB MIDI is now disabled; change MIDI "
                       "Route on the NTS-1 mkII and click Refresh."),
        12000);
}

void MainWindow::autoReceiveStartupData()
{
    if (m_busy || !m_selectedDevice.valid)
        return;

    auto program = std::make_shared<Program>();
    auto global = std::make_shared<QByteArray>();
    runDeviceTask(
        QStringLiteral("Reading current program and global data…"),
        [program, global](DeviceSession &session, QString *error, const ProgressFn &) {
            if (!session.receiveProgram(program.get(), error))
                return false;
            if (!session.receiveGlobal(global.get(), error))
                return false;
            return true;
        },
        [this, program, global] {
            m_workspace.setCurrentProgram(*program);
            m_workspace.global() = GlobalData::fromDeviceBytes(*global);
            markModified();
            refreshAll();
            statusBar()->showMessage(QStringLiteral("Device data received"), 4000);
        });
}

void MainWindow::setWorkspace(const Workspace &workspace)
{
    m_workspace = workspace;
    refreshAll();
    updateTitle();
}

void MainWindow::refreshAll()
{
    m_programPage->refresh();
    m_oscPage->refresh();
    m_modPage->refresh();
    m_delPage->refresh();
    m_revPage->refresh();
    m_globalPage->refresh();
}

void MainWindow::markModified()
{
    m_workspace.setModified(true);
    updateTitle();
}

void MainWindow::updateTitle()
{
    const QString name = m_workspace.filePath().isEmpty()
                             ? QStringLiteral("Untitled")
                             : QFileInfo(m_workspace.filePath()).fileName();
    setWindowTitle(QStringLiteral("nts-nts-kontrol — %1%2")
                       .arg(name, m_workspace.modified() ? QStringLiteral(" *") : QString()));
}

bool MainWindow::maybeSave()
{
    if (!m_workspace.modified())
        return true;
    const auto answer = QMessageBox::warning(
        this, QStringLiteral("nts-nts-kontrol"),
        QStringLiteral("Do you want to save the changes to the workspace?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return saveWorkspace();
    return true;
}

void MainWindow::newWorkspace()
{
    if (!maybeSave())
        return;
    m_workspace.clear();
    m_workspace.setFilePath(QString());
    refreshAll();
    updateTitle();
    statusBar()->showMessage(QStringLiteral("New workspace"), 3000);
}

void MainWindow::openWorkspace()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Workspace"), QString(),
        QStringLiteral("NTS-1 mkII workspace (*.nts1mkii);;All files (*)"));
    if (path.isEmpty())
        return;

    Workspace workspace;
    QString error;
    if (!WorkspaceFile::read(path, &workspace, &error)) {
        showError(QStringLiteral("Open Workspace"), error);
        return;
    }
    setWorkspace(workspace);
    statusBar()->showMessage(QStringLiteral("Opened %1").arg(path), 4000);
}

bool MainWindow::saveWorkspace()
{
    if (m_workspace.filePath().isEmpty())
        return saveWorkspaceAs();
    QString error;
    if (!WorkspaceFile::write(m_workspace.filePath(), &m_workspace, &error)) {
        showError(QStringLiteral("Save Workspace"), error);
        return false;
    }
    updateTitle();
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(m_workspace.filePath()), 4000);
    return true;
}

bool MainWindow::saveWorkspaceAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Workspace As"), QStringLiteral("Untitled.nts1mkii"),
        QStringLiteral("NTS-1 mkII workspace (*.nts1mkii);;All files (*)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".nts1mkii"), Qt::CaseInsensitive))
        path += QStringLiteral(".nts1mkii");
    QString error;
    if (!WorkspaceFile::write(path, &m_workspace, &error)) {
        showError(QStringLiteral("Save Workspace"), error);
        return false;
    }
    updateTitle();
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(path), 4000);
    return true;
}

void MainWindow::showPreferences()
{
    PreferencesDialog dialog(this);
    dialog.setWarnBeforeWrite(m_warnBeforeWrite);
    dialog.setAutoReceive(m_autoReceive);
    dialog.setBackendPreference(m_backendPreference);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_warnBeforeWrite = dialog.warnBeforeWrite();
    m_autoReceive = dialog.autoReceive();
    m_backendPreference = dialog.backendPreference();
    saveSettings();
    refreshDevices();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, QStringLiteral("About nts-nts-kontrol"),
        QStringLiteral("<h3>nts-nts-kontrol for Linux</h3>"
                       "<p>Native NTS-1 mkII librarian and editor.</p>"
                       "<p>Program, user oscillator/effect, global, and workspace "
                       "management over PipeWire MIDI or raw ALSA MIDI. Compatible with the official "
                       "KORG editor file formats.</p>"
                       "<p>This is an independent implementation and is not "
                       "affiliated with KORG Inc.</p>"));
}

bool MainWindow::confirmWrite(const QString &description, bool force)
{
    if (!force && !m_warnBeforeWrite)
        return true;
    return QMessageBox::question(this, QStringLiteral("Confirm Write"),
                                 description + QStringLiteral("\n\nWrite to the device?"),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
           == QMessageBox::Yes;
}

void MainWindow::showError(const QString &title, const QString &message)
{
    QMessageBox::critical(this, title, message);
}

void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    m_deviceCombo->setEnabled(!busy && !m_scanning);
    m_refreshButton->setEnabled(!busy && !m_scanning);
    m_tabs->setEnabled(!busy);
    if (m_receiveAllAction)
        m_receiveAllAction->setEnabled(!busy);
    if (m_sendAllAction)
        m_sendAllAction->setEnabled(!busy);
}

void MainWindow::runDeviceTask(const QString &title, const TaskFn &task,
                               const std::function<void()> &onSuccess)
{
    if (m_busy || m_scanning)
        return;
    if (!m_selectedDevice.valid) {
        QMessageBox::information(this, QStringLiteral("No Device"),
                                 QStringLiteral("Select a connected NTS-1 mkII first."));
        return;
    }

    setBusy(true);
    auto *dialog = new QProgressDialog(title, QString(), 0, 0, this);
    dialog->setWindowTitle(QStringLiteral("nts-nts-kontrol"));
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setCancelButton(nullptr);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    dialog->show();
    QPointer<QProgressDialog> dialogGuard(dialog);

    ProgressFn progress = [dialogGuard](int done, int total) {
        if (!dialogGuard)
            return;
        QMetaObject::invokeMethod(dialogGuard.data(), [dialogGuard, done, total] {
            if (!dialogGuard)
                return;
            if (total > 0) {
                dialogGuard->setMaximum(total);
                dialogGuard->setValue(done);
            }
        }, Qt::QueuedConnection);
    };

    auto *watcher = new QFutureWatcher<TaskResult>(this);
    connect(watcher, &QFutureWatcher<TaskResult>::finished, this,
            [this, watcher, dialogGuard, onSuccess] {
                const TaskResult result = watcher->result();
                watcher->deleteLater();
                if (dialogGuard) {
                    dialogGuard->close();
                    dialogGuard->deleteLater();
                }
                setBusy(false);
                if (!result.ok) {
                    QMessageBox::critical(this, QStringLiteral("nts-nts-kontrol"),
                                          result.error.isEmpty()
                                              ? QStringLiteral("The operation failed.")
                                              : result.error);
                    return;
                }
                if (onSuccess)
                    onSuccess();
            });

    const DeviceInfo device = m_selectedDevice;
    watcher->setFuture(QtConcurrent::run([device, task, progress]() -> TaskResult {
        TaskResult result;
        DeviceSession session;
        QString error;
        if (!session.open(device, &error)) {
            result.error = error;
            return result;
        }
        result.ok = task(session, &error, progress);
        result.error = error;
        return result;
    }));
}

void MainWindow::receiveCurrentProgram()
{
    auto program = std::make_shared<Program>();
    runDeviceTask(
        QStringLiteral("Receiving current program…"),
        [program](DeviceSession &session, QString *error, const ProgressFn &) {
            return session.receiveProgram(program.get(), error);
        },
        [this, program] {
            m_workspace.setCurrentProgram(*program);
            markModified();
            refreshAll();
            statusBar()->showMessage(QStringLiteral("Current program received"), 4000);
        });
}

void MainWindow::sendCurrentProgram()
{
    const Program program = m_workspace.currentProgram();
    if (!program.isValid()) {
        QMessageBox::information(this, QStringLiteral("Send Program"),
                                 QStringLiteral("Receive a program from the device first."));
        return;
    }
    if (!confirmWrite(QStringLiteral("Write the current program to the NTS-1 mkII?")))
        return;
    runDeviceTask(
        QStringLiteral("Writing current program…"),
        [program](DeviceSession &session, QString *error, const ProgressFn &) {
            return session.sendProgram(program, error);
        },
        [this] { statusBar()->showMessage(QStringLiteral("Current program written"), 4000); });
}

void MainWindow::sendProgramToDevice(const Program &program)
{
    if (!program.isValid())
        return;
    if (!confirmWrite(QStringLiteral("Write \"%1\" to the NTS-1 mkII?")
                          .arg(program.displayName())))
        return;
    runDeviceTask(
        QStringLiteral("Writing program…"),
        [program](DeviceSession &session, QString *error, const ProgressFn &) {
            return session.sendProgram(program, error);
        },
        [this, program] {
            m_workspace.setCurrentProgram(program);
            refreshAll();
            statusBar()->showMessage(QStringLiteral("Program written"), 4000);
        });
}

void MainWindow::receiveAllData()
{
    if (!maybeSave())
        return;

    auto program = std::make_shared<Program>();
    auto global = std::make_shared<QByteArray>();
    auto units = std::make_shared<UnitLibrary>();

    runDeviceTask(
        QStringLiteral("Receiving all data…"),
        [program, global, units](DeviceSession &session, QString *error,
                                 const ProgressFn &progress) {
            int total = 2;
            for (Module module : {Module::Osc, Module::ModFx, Module::DelFx, Module::RevFx})
                total += slotCount(module);
            int step = 0;
            if (!session.receiveProgram(program.get(), error))
                return false;
            progress(++step, total);
            if (!session.receiveGlobal(global.get(), error))
                return false;
            progress(++step, total);
            for (Module module : {Module::Osc, Module::ModFx, Module::DelFx, Module::RevFx}) {
                QVector<UserUnit> &target = units->forModule(module);
                for (int slot = 0; slot < slotCount(module); ++slot) {
                    SlotInfo status;
                    if (!session.getSlotStatus(module, slot, &status, error))
                        return false;
                    if (status.present) {
                        QByteArray raw;
                        QString downloadError;
                        if (!session.downloadUnit(module, slot, &raw, &downloadError)) {
                            *error = QStringLiteral("%1 slot %2: %3")
                                         .arg(moduleDisplayName(module))
                                         .arg(slot)
                                         .arg(downloadError);
                            return false;
                        }
                        UserUnit unit = UserUnit::fromBytes(raw);
                        if (status.valid)
                            unit.header() = status.header;
                        target[slot] = unit;
                    } else {
                        target[slot] = UserUnit();
                    }
                    progress(++step, total);
                }
            }
            return true;
        },
        [this, program, global, units] {
            m_workspace.setCurrentProgram(*program);
            m_workspace.global() = GlobalData::fromDeviceBytes(*global);
            m_workspace.units() = *units;
            markModified();
            refreshAll();
            statusBar()->showMessage(QStringLiteral("All data received"), 4000);
        });
}

void MainWindow::sendAllData()
{
    const Program program = m_workspace.currentProgram();
    const bool hasGlobal = m_workspace.global().isValid();
    const QByteArray global = hasGlobal ? m_workspace.global().deviceBytes() : QByteArray();
    const GlobalData globalData = GlobalData::fromDeviceBytes(global);
    const UnitLibrary units = m_workspace.units();
    if (!program.isValid() && !m_workspace.global().isValid()) {
        QMessageBox::information(this, QStringLiteral("Send All Data"),
                                 QStringLiteral("There is no device data to send."));
        return;
    }

    QString prompt = QStringLiteral("Write the current program, global data, and all "
                                    "non-empty user units to the NTS-1 mkII?");
    if (globalData.usbMidiDisabled()) {
        prompt += QStringLiteral(
            "\n\nWARNING: MIDI Route is set to MIDI only. The global block is "
            "written last, but USB MIDI will then be disabled and the editor "
            "will lose its connection.");
    }
    if (!confirmWrite(prompt, globalData.usbMidiDisabled()))
        return;

    runDeviceTask(
        QStringLiteral("Sending all data…"),
        [program, global, units](DeviceSession &session, QString *error,
                                 const ProgressFn &progress) {
            int total = (program.isValid() ? 1 : 0) + (!global.isEmpty() ? 1 : 0);
            for (Module module : {Module::Osc, Module::ModFx, Module::DelFx, Module::RevFx}) {
                for (const UserUnit &unit : units.forModule(module)) {
                    if (!unit.isEmpty())
                        ++total;
                }
            }
            if (total == 0)
                return true;
            int step = 0;
            if (program.isValid()) {
                if (!session.sendProgram(program, error))
                    return false;
                progress(++step, total);
            }
            for (Module module : {Module::Osc, Module::ModFx, Module::DelFx, Module::RevFx}) {
                const QVector<UserUnit> &source = units.forModule(module);
                for (int slot = 0; slot < source.size(); ++slot) {
                    const UserUnit &unit = source.at(slot);
                    if (unit.isEmpty())
                        continue;
                    if (!session.uploadUnit(module, slot, unit.bytes(), error)) {
                        *error = QStringLiteral("%1 slot %2: %3")
                                     .arg(moduleDisplayName(module))
                                     .arg(slot)
                                     .arg(*error);
                        return false;
                    }
                    progress(++step, total);
                }
            }
            // Send global last: MIDI Route = MIDI only disables the USB
            // transport used for everything above.
            if (!global.isEmpty()) {
                if (!session.sendGlobal(global, error))
                    return false;
                progress(++step, total);
            }
            return true;
        },
        [this, globalData] {
            if (globalData.usbMidiDisabled()) {
                markTransportLost();
                return;
            }
            statusBar()->showMessage(QStringLiteral("All data sent"), 4000);
        });
}

void MainWindow::receiveUnitModule(Module module)
{
    auto units = std::make_shared<QVector<UserUnit>>();
    units->resize(slotCount(module));
    runDeviceTask(
        QStringLiteral("Receiving %1 data…").arg(moduleDisplayName(module)),
        [module, units](DeviceSession &session, QString *error, const ProgressFn &progress) {
            const int total = slotCount(module);
            for (int slot = 0; slot < total; ++slot) {
                SlotInfo status;
                if (!session.getSlotStatus(module, slot, &status, error))
                    return false;
                if (status.present) {
                    QByteArray raw;
                    QString downloadError;
                    if (!session.downloadUnit(module, slot, &raw, &downloadError)) {
                        *error = QStringLiteral("%1 slot %2: %3")
                                     .arg(moduleDisplayName(module))
                                     .arg(slot)
                                     .arg(downloadError);
                        return false;
                    }
                    UserUnit unit = UserUnit::fromBytes(raw);
                    if (status.valid)
                        unit.header() = status.header;
                    (*units)[slot] = unit;
                } else {
                    (*units)[slot] = UserUnit();
                }
                progress(slot + 1, total);
            }
            return true;
        },
        [this, module, units] {
            m_workspace.units().forModule(module) = *units;
            markModified();
            refreshAll();
            statusBar()->showMessage(
                QStringLiteral("%1 data received").arg(moduleDisplayName(module)), 4000);
        });
}

void MainWindow::sendUnitModule(Module module)
{
    const QVector<UserUnit> source = m_workspace.units().forModule(module);
    int count = 0;
    for (const UserUnit &unit : source) {
        if (!unit.isEmpty())
            ++count;
    }
    if (count == 0) {
        QMessageBox::information(this, QStringLiteral("Send User Unit"),
                                 QStringLiteral("There are no loaded units in this tab."));
        return;
    }
    if (!confirmWrite(QStringLiteral("Write %1 user unit(s) from the %2 tab to the device?")
                          .arg(count)
                          .arg(moduleDisplayName(module))))
        return;

    runDeviceTask(
        QStringLiteral("Sending %1 data…").arg(moduleDisplayName(module)),
        [module, source](DeviceSession &session, QString *error, const ProgressFn &progress) {
            const int total = source.size();
            int step = 0;
            for (int slot = 0; slot < total; ++slot) {
                const UserUnit &unit = source.at(slot);
                if (!unit.isEmpty()) {
                    QString uploadError;
                    if (!session.uploadUnit(module, slot, unit.bytes(), &uploadError)) {
                        *error = QStringLiteral("%1 slot %2: %3")
                                     .arg(moduleDisplayName(module))
                                     .arg(slot)
                                     .arg(uploadError);
                        return false;
                    }
                }
                progress(++step, total);
            }
            return true;
        },
        [this, module] {
            statusBar()->showMessage(
                QStringLiteral("%1 data sent").arg(moduleDisplayName(module)), 4000);
        });
}

void MainWindow::sendUnitSlot(Module module, int slot)
{
    const QVector<UserUnit> &units = m_workspace.units().forModule(module);
    if (slot < 0 || slot >= units.size() || units.at(slot).isEmpty())
        return;
    const UserUnit unit = units.at(slot);
    if (!confirmWrite(QStringLiteral("Write \"%1\" to %2 slot %3?")
                          .arg(unit.displayName(), moduleDisplayName(module))
                          .arg(slot)))
        return;

    runDeviceTask(
        QStringLiteral("Writing user unit…"),
        [module, slot, unit](DeviceSession &session, QString *error,
                             const ProgressFn &progress) {
            return session.uploadUnit(module, slot, unit.bytes(), error,
                                      [progress](int done, int total) {
                                          progress(done, total);
                                      });
        },
        [this, module, slot] {
            statusBar()->showMessage(QStringLiteral("%1 slot %2 written")
                                         .arg(moduleDisplayName(module))
                                         .arg(slot),
                                     4000);
        });
}

void MainWindow::receiveGlobal()
{
    auto global = std::make_shared<QByteArray>();
    runDeviceTask(
        QStringLiteral("Receiving global data…"),
        [global](DeviceSession &session, QString *error, const ProgressFn &) {
            return session.receiveGlobal(global.get(), error);
        },
        [this, global] {
            m_workspace.global() = GlobalData::fromDeviceBytes(*global);
            markModified();
            refreshAll();
            statusBar()->showMessage(QStringLiteral("Global data received"), 4000);
        });
}

void MainWindow::sendGlobal()
{
    if (!m_workspace.global().isValid()) {
        QMessageBox::information(this, QStringLiteral("Write Global Data"),
                                 QStringLiteral("Receive global data from the device first."));
        return;
    }
    const QByteArray global = m_workspace.global().deviceBytes();
    const GlobalData data = GlobalData::fromDeviceBytes(global);
    QString prompt = QStringLiteral("Write the global settings to the NTS-1 mkII?");
    if (data.usbMidiDisabled()) {
        prompt += QStringLiteral(
            "\n\nWARNING: MIDI Route is set to MIDI only. This disables USB MIDI, "
            "so the editor will lose its connection until MIDI Route is changed "
            "back on the device.");
    }
    if (!confirmWrite(prompt, data.usbMidiDisabled()))
        return;
    runDeviceTask(
        QStringLiteral("Writing global data…"),
        [global](DeviceSession &session, QString *error, const ProgressFn &) {
            return session.sendGlobal(global, error);
        },
        [this, data] {
            if (data.usbMidiDisabled()) {
                markTransportLost();
                return;
            }
            updateDeviceChannel(data.midiChannel());
            statusBar()->showMessage(QStringLiteral("Global data written"), 4000);
        });
}

UnitPage *MainWindow::currentUnitPage() const
{
    return qobject_cast<UnitPage *>(m_tabs->currentWidget());
}

UnitPage *MainWindow::defaultUnitPage() const
{
    if (auto *page = currentUnitPage())
        return page;
    return m_oscPage;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_busy) {
        QMessageBox::information(this, QStringLiteral("Transfer in Progress"),
                                 QStringLiteral("Please wait for the current transfer to finish."));
        event->ignore();
        return;
    }
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    saveSettings();
    event->accept();
}

} // namespace korg
