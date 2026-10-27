#pragma once

#include "core/DeviceSession.h"
#include "core/model/Workspace.h"

#include <QList>
#include <QMainWindow>

#include <functional>

class QAction;
class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;

namespace korg {

class GlobalPage;
class ProgramPage;
class UnitPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct ScanResult {
        QList<DeviceInfo> devices;
        QString error;
    };

    using ProgressFn = std::function<void(int, int)>;
    using TaskFn = std::function<bool(DeviceSession &, QString *, const ProgressFn &)>;

    void buildUi();
    void buildMenus();
    void loadSettings();
    void saveSettings();

    void refreshDevices();
    void onDeviceSelected(int index);
    void updateDeviceStatus();
    void updateDeviceChannel(int channel);
    void markTransportLost();
    void autoReceiveStartupData();

    void setWorkspace(const Workspace &workspace);
    void refreshAll();
    void markModified();
    void updateTitle();
    bool maybeSave();

    void newWorkspace();
    void openWorkspace();
    bool saveWorkspace();
    bool saveWorkspaceAs();
    void showPreferences();
    void showAbout();

    bool confirmWrite(const QString &description, bool force = false);
    void showError(const QString &title, const QString &message);

    void runDeviceTask(const QString &title, const TaskFn &task,
                       const std::function<void()> &onSuccess = {});

    void receiveCurrentProgram();
    void sendCurrentProgram();
    void sendProgramToDevice(const Program &program);
    void receiveAllData();
    void sendAllData();
    void receiveUnitModule(Module module);
    void sendUnitModule(Module module);
    void sendUnitSlot(Module module, int slot);
    void receiveGlobal();
    void sendGlobal();

    UnitPage *currentUnitPage() const;
    UnitPage *defaultUnitPage() const;
    void setBusy(bool busy);

    Workspace m_workspace;
    QList<DeviceInfo> m_devices;
    DeviceInfo m_selectedDevice;
    bool m_busy = false;
    bool m_scanning = false;
    bool m_warnBeforeWrite = true;
    bool m_autoReceive = false;
    MidiBackendPreference m_backendPreference = MidiBackendPreference::Auto;

    QComboBox *m_deviceCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLabel *m_deviceStatus = nullptr;
    QTabWidget *m_tabs = nullptr;
    ProgramPage *m_programPage = nullptr;
    UnitPage *m_oscPage = nullptr;
    UnitPage *m_modPage = nullptr;
    UnitPage *m_delPage = nullptr;
    UnitPage *m_revPage = nullptr;
    GlobalPage *m_globalPage = nullptr;

    QAction *m_receiveAllAction = nullptr;
    QAction *m_sendAllAction = nullptr;
};

} // namespace korg
