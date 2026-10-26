#include "core/DeviceSession.h"
#include "core/file/WorkspaceFile.h"
#include "core/model/Program.h"
#include "core/model/UserUnit.h"
#include "core/model/GlobalData.h"
#include "core/model/Workspace.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTextStream>

#include <cstdio>

using namespace korg;

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

MidiBackendPreference backendPreference(const QCommandLineParser &parser)
{
    return midiBackendPreferenceFromString(parser.value(QStringLiteral("backend")));
}

DeviceInfo chooseDevice(const QCommandLineParser &parser, QString *error)
{
    QString scanError;
    const QList<DeviceInfo> devices = DeviceSession::scan(backendPreference(parser), &scanError);
    if (devices.isEmpty()) {
        *error = scanError;
        return {};
    }

    bool ok = false;
    int index = 0;
    if (parser.isSet(QStringLiteral("device")))
        index = parser.value(QStringLiteral("device")).toInt(&ok);
    if (!ok || index < 0 || index >= devices.size())
        index = 0;
    return devices.at(index);
}

Module parseModule(const QString &value, bool *ok)
{
    const QString lower = value.toLower();
    *ok = true;
    if (lower == QStringLiteral("osc") || lower == QStringLiteral("oscillator"))
        return Module::Osc;
    if (lower == QStringLiteral("modfx") || lower == QStringLiteral("mod"))
        return Module::ModFx;
    if (lower == QStringLiteral("delfx") || lower == QStringLiteral("delay"))
        return Module::DelFx;
    if (lower == QStringLiteral("revfx") || lower == QStringLiteral("reverb"))
        return Module::RevFx;
    *ok = false;
    return Module::Osc;
}

int cmdList(const QCommandLineParser &parser)
{
    QString error;
    const QList<DeviceInfo> devices = DeviceSession::scan(backendPreference(parser), &error);
    if (devices.isEmpty()) {
        err() << "No device: " << error << "\n";
        return 1;
    }
    for (int i = 0; i < devices.size(); ++i) {
        const DeviceInfo &device = devices.at(i);
        out() << i << ": " << device.displayName() << " [" << device.port.hwName()
              << "] channel " << (device.channel + 1) << " SDK " << device.versionString()
              << "\n";
    }
    return 0;
}

int cmdProgram(const QCommandLineParser &parser)
{
    QString error;
    const DeviceInfo info = chooseDevice(parser, &error);
    if (!info.valid) {
        err() << error << "\n";
        return 1;
    }

    DeviceSession session;
    if (!session.open(info, &error)) {
        err() << error << "\n";
        return 1;
    }

    if (parser.isSet(QStringLiteral("dump"))) {
        Program program;
        if (!session.receiveProgram(&program, &error)) {
            err() << error << "\n";
            return 1;
        }
        const QString path = parser.value(QStringLiteral("dump"));
        if (!WorkspaceFile::writeProgramFile(path, program, &error)) {
            err() << error << "\n";
            return 1;
        }
        out() << "Received program " << program.displayName() << " to " << path << "\n";
        return 0;
    }

    if (parser.isSet(QStringLiteral("send"))) {
        const QString path = parser.value(QStringLiteral("send"));
        Program program;
        if (!WorkspaceFile::readProgramFile(path, &program, &error)) {
            err() << error << "\n";
            return 1;
        }
        if (!session.sendProgram(program, &error)) {
            err() << error << "\n";
            return 1;
        }
        out() << "Program sent.\n";
        return 0;
    }

    err() << "Use --dump <file> or --send <file>.\n";
    return 1;
}

int cmdGlobal(const QCommandLineParser &parser)
{
    QString error;
    const DeviceInfo info = chooseDevice(parser, &error);
    if (!info.valid) {
        err() << error << "\n";
        return 1;
    }

    DeviceSession session;
    if (!session.open(info, &error)) {
        err() << error << "\n";
        return 1;
    }

    if (parser.isSet(QStringLiteral("dump"))) {
        QByteArray global;
        if (!session.receiveGlobal(&global, &error)) {
            err() << error << "\n";
            return 1;
        }
        QFile file(parser.value(QStringLiteral("dump")));
        if (!file.open(QIODevice::WriteOnly)) {
            err() << file.errorString() << "\n";
            return 1;
        }
        file.write(global);
        out() << "Global data: " << global.toHex(' ') << "\n";
        return 0;
    }

    if (parser.isSet(QStringLiteral("send"))) {
        QFile file(parser.value(QStringLiteral("send")));
        if (!file.open(QIODevice::ReadOnly)) {
            err() << file.errorString() << "\n";
            return 1;
        }
        const QByteArray global = file.readAll();
        if (!session.sendGlobal(global, &error)) {
            err() << error << "\n";
            return 1;
        }
        out() << "Global data sent.\n";
        return 0;
    }

    err() << "Use --dump <file> or --send <file>.\n";
    return 1;
}

int cmdStatus(const QCommandLineParser &parser)
{
    QString error;
    const DeviceInfo info = chooseDevice(parser, &error);
    if (!info.valid) {
        err() << error << "\n";
        return 1;
    }

    bool ok = false;
    const Module module = parseModule(parser.value(QStringLiteral("module")), &ok);
    if (!ok) {
        err() << "Invalid module.\n";
        return 1;
    }
    const int slot = parser.value(QStringLiteral("slot")).toInt(&ok);
    if (!ok) {
        err() << "Invalid slot.\n";
        return 1;
    }

    DeviceSession session;
    if (!session.open(info, &error)) {
        err() << error << "\n";
        return 1;
    }

    SlotInfo slotInfo;
    if (!session.getSlotStatus(module, slot, &slotInfo, &error)) {
        err() << error << "\n";
        return 1;
    }

    if (!slotInfo.present) {
        out() << moduleName(module) << " " << slot << " - EMPTY\n";
        return 0;
    }
    out() << moduleName(module) << " " << slot << " - "
          << (slotInfo.valid ? slotInfo.header.name : QStringLiteral("<invalid>")) << "\n";
    if (slotInfo.valid) {
        out() << "  target=0x" << QString::number(slotInfo.header.target, 16)
              << " api=" << slotInfo.header.apiString()
              << " dev=" << slotInfo.header.devIdString()
              << " unit=" << slotInfo.header.unitIdString()
              << " version=" << slotInfo.header.versionString() << "\n";
        for (const UnitParam &param : slotInfo.header.params)
            out() << "  " << param.name << " [" << param.min << ", " << param.max << "]\n";
    }
    return 0;
}

int cmdTransfer(const QCommandLineParser &parser, bool upload)
{
    QString error;
    const DeviceInfo info = chooseDevice(parser, &error);
    if (!info.valid) {
        err() << error << "\n";
        return 1;
    }

    bool ok = false;
    const Module module = parseModule(parser.value(QStringLiteral("module")), &ok);
    if (!ok) {
        err() << "Invalid module.\n";
        return 1;
    }
    const int slot = parser.value(QStringLiteral("slot")).toInt(&ok);
    if (!ok) {
        err() << "Invalid slot.\n";
        return 1;
    }
    const QString path = parser.value(QStringLiteral("file"));
    if (path.isEmpty()) {
        err() << "Missing --file.\n";
        return 1;
    }

    DeviceSession session;
    if (!session.open(info, &error)) {
        err() << error << "\n";
        return 1;
    }

    if (upload) {
        UserUnit unit;
        if (!WorkspaceFile::readUnitFile(path, &unit, &error)) {
            err() << error << "\n";
            return 1;
        }
        if (!session.uploadUnit(module, slot, unit.bytes(), &error,
                                [](int done, int total) {
                                    out() << "  packet " << done << "/" << total << "\n";
                                })) {
            err() << error << "\n";
            return 1;
        }
        out() << "Uploaded " << path << " to " << moduleName(module) << " " << slot << "\n";
    } else {
        QByteArray bytes;
        if (!session.downloadUnit(module, slot, &bytes, &error)) {
            err() << error << "\n";
            return 1;
        }
        const UserUnit unit = UserUnit::fromBytes(bytes);
        if (!WorkspaceFile::writeUnitFile(path, unit, &error)) {
            err() << error << "\n";
            return 1;
        }
        out() << "Downloaded " << moduleName(module) << " " << slot << " ("
              << bytes.size() << " bytes, " << unit.displayName() << ") to " << path
              << "\n";
    }
    return 0;
}

int cmdRewrite(const QCommandLineParser &parser)
{
    const QStringList args = parser.positionalArguments();
    if (args.size() < 3) {
        err() << "Usage: nts-nts-kontrol-cli rewrite <input.nts1mkii> <output.nts1mkii>\n";
        return 1;
    }
    Workspace workspace;
    QString error;
    if (!WorkspaceFile::read(args.at(1), &workspace, &error)) {
        err() << error << "\n";
        return 1;
    }
    if (parser.isSet(QStringLiteral("global"))) {
        QFile globalFile(parser.value(QStringLiteral("global")));
        if (!globalFile.open(QIODevice::ReadOnly)) {
            err() << globalFile.errorString() << "\n";
            return 1;
        }
        const QByteArray globalBytes = globalFile.readAll();
        if (!GlobalData::looksLikeDeviceData(globalBytes)) {
            err() << "Invalid global data file\n";
            return 1;
        }
        workspace.global() = GlobalData::fromDeviceBytes(globalBytes);
    }
    if (!WorkspaceFile::write(args.at(2), &workspace, &error)) {
        err() << error << "\n";
        return 1;
    }
    out() << "Rewrote " << args.at(2) << " (" << workspace.library().size()
          << " library slots)\n";
    return 0;
}

int cmdMakeWorkspace(const QCommandLineParser &parser)
{
    const QStringList args = parser.positionalArguments();
    if (args.size() < 2) {
        err() << "Usage: nts-nts-kontrol-cli mkws <output.nts1mkii> [program.nts1mkiiprog]\n";
        return 1;
    }
    Workspace workspace;
    QString error;
    if (args.size() >= 3) {
        Program program;
        if (!WorkspaceFile::readProgramFile(args.at(2), &program, &error)) {
            err() << error << "\n";
            return 1;
        }
        workspace.setCurrentProgram(program);
    }
    if (parser.isSet(QStringLiteral("global"))) {
        QFile globalFile(parser.value(QStringLiteral("global")));
        if (!globalFile.open(QIODevice::ReadOnly)) {
            err() << globalFile.errorString() << "\n";
            return 1;
        }
        const QByteArray globalBytes = globalFile.readAll();
        if (!GlobalData::looksLikeDeviceData(globalBytes)) {
            err() << "Invalid global data file\n";
            return 1;
        }
        workspace.global() = GlobalData::fromDeviceBytes(globalBytes);
    }
    if (!WorkspaceFile::write(args.at(1), &workspace, &error)) {
        err() << error << "\n";
        return 1;
    }
    out() << "Wrote " << args.at(1) << "\n";
    return 0;
}

int cmdClear(const QCommandLineParser &parser)
{
    QString error;
    const DeviceInfo info = chooseDevice(parser, &error);
    if (!info.valid) {
        err() << error << "\n";
        return 1;
    }
    bool ok = false;
    const Module module = parseModule(parser.value(QStringLiteral("module")), &ok);
    const int slot = parser.value(QStringLiteral("slot")).toInt(&ok);
    if (!ok) {
        err() << "Invalid module/slot.\n";
        return 1;
    }
    DeviceSession session;
    if (!session.open(info, &error)) {
        err() << error << "\n";
        return 1;
    }
    if (!session.clearSlot(module, slot, &error)) {
        err() << error << "\n";
        return 1;
    }
    out() << "Cleared.\n";
    return 0;
}

int cmdSwap(const QCommandLineParser &parser)
{
    QString error;
    const DeviceInfo info = chooseDevice(parser, &error);
    if (!info.valid) {
        err() << error << "\n";
        return 1;
    }
    bool ok = false;
    const Module module = parseModule(parser.value(QStringLiteral("module")), &ok);
    const int a = parser.value(QStringLiteral("a")).toInt(&ok);
    const int b = parser.value(QStringLiteral("b")).toInt(&ok);
    if (!ok) {
        err() << "Invalid module/slot.\n";
        return 1;
    }
    DeviceSession session;
    if (!session.open(info, &error)) {
        err() << error << "\n";
        return 1;
    }
    if (!session.swapSlots(module, a, b, &error)) {
        err() << error << "\n";
        return 1;
    }
    out() << "Swapped.\n";
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("nts-nts-kontrol-cli"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("KORG NTS-1 mkII librarian CLI"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("device"), QStringLiteral("Device index from 'list'"),
                      QStringLiteral("index"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("backend"),
                      QStringLiteral("MIDI backend: auto|pipewire|alsa"),
                      QStringLiteral("backend"), QStringLiteral("auto")});
    parser.addOption({QStringLiteral("module"), QStringLiteral("Module: osc/modfx/delfx/revfx"),
                      QStringLiteral("module"), QStringLiteral("osc")});
    parser.addOption({QStringLiteral("slot"), QStringLiteral("Slot number"), QStringLiteral("slot")});
    parser.addOption({QStringLiteral("file"), QStringLiteral("Unit file"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("dump"), QStringLiteral("Dump to file"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("send"), QStringLiteral("Send from file"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("a"), QStringLiteral("First slot"), QStringLiteral("slot")});
    parser.addOption({QStringLiteral("b"), QStringLiteral("Second slot"), QStringLiteral("slot")});
    parser.addOption({QStringLiteral("global"), QStringLiteral("Global data file"), QStringLiteral("file")});
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("list|program|global|status|download|upload|clear|swap"));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty())
        return 0;
    const QString command = args.first().toLower();

    if (command == QStringLiteral("list"))
        return cmdList(parser);
    if (command == QStringLiteral("program"))
        return cmdProgram(parser);
    if (command == QStringLiteral("global"))
        return cmdGlobal(parser);
    if (command == QStringLiteral("status"))
        return cmdStatus(parser);
    if (command == QStringLiteral("download"))
        return cmdTransfer(parser, false);
    if (command == QStringLiteral("upload"))
        return cmdTransfer(parser, true);
    if (command == QStringLiteral("rewrite"))
        return cmdRewrite(parser);
    if (command == QStringLiteral("mkws"))
        return cmdMakeWorkspace(parser);
    if (command == QStringLiteral("clear"))
        return cmdClear(parser);
    if (command == QStringLiteral("swap"))
        return cmdSwap(parser);

    err() << "Unknown command: " << command << "\n";
    return 1;
}
