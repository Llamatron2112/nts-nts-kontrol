#include "core/file/WorkspaceFile.h"
#include "core/midi/MidiBackend.h"
#include "core/midi/SysExAssembler.h"
#include "core/model/GlobalData.h"
#include "core/model/Program.h"
#include "core/model/UserUnit.h"
#include "core/model/Workspace.h"
#include "core/protocol/KorgProtocol.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace korg;

class CoreTest : public QObject
{
    Q_OBJECT

private slots:
    void sevenBitRoundTrip()
    {
        QByteArray host;
        for (int i = 0; i < 100; ++i)
            host.append(char((i * 37) & 0xff));
        const QByteArray midi = encode7bit(host);
        QCOMPARE(decode7bit(midi), host);
        for (char ch : midi)
            QVERIFY(quint8(ch) < 0x80);
    }

    void crc32MatchesZlib()
    {
        const QByteArray data = QByteArrayLiteral("123456789");
        QCOMPARE(crc32(data), quint32(0xcbf43926));
    }

    void programValidation()
    {
        QByteArray bytes(Program::kSize, '\0');
        bytes.replace(0, 4, "PROG");
        bytes.replace(Program::kSize - 4, 4, "PRED");
        QVERIFY(Program::looksValid(bytes));
        Program program = Program::fromBytes(bytes);
        program.setName(QStringLiteral("Test Program"));
        QCOMPARE(program.name(), QStringLiteral("Test Program"));
    }

    void searchFrameParsing()
    {
        const QByteArray frame = QByteArray::fromHex(
            "f042500106557301010003000100f7");
        const ParsedSysex message = parseSysex(frame);
        QCOMPARE(message.kind, ParsedSysex::Kind::SearchReply);
        const SearchReply reply = parseSearchReply(message);
        QVERIFY(reply.valid);
        QCOMPARE(reply.channel, 6);
        QCOMPARE(reply.family, quint16(0x0173));
    }

    void statusFrameParsing()
    {
        const QByteArray ok = QByteArray::fromHex("f0423600017323f7");
        const ParsedSysex okMessage = parseSysex(ok);
        QVERIFY(okMessage.isLogue());
        QVERIFY(okMessage.isStatus());
        QVERIFY(okMessage.isCompleted());

        const QByteArray error = QByteArray::fromHex("f0423600017324f7");
        const ParsedSysex errorMessage = parseSysex(error);
        QVERIFY(errorMessage.isLogue());
        QVERIFY(errorMessage.isStatus());
        QVERIFY(!errorMessage.isCompleted());
        QCOMPARE(errorMessage.id, quint8(0x24));
    }

    void userSlotMessageChunking()
    {
        const QByteArray unit(9000, 'x');
        const QList<QByteArray> messages = userSlotDataMessages(6, Module::Osc, 3, unit);
        QCOMPARE(messages.size(), 5);
        for (const QByteArray &message : messages)
            QVERIFY(message.size() <= 4096);
    }

    void globalFields()
    {
        const QList<GlobalField> fields = GlobalData::fields();
        QCOMPARE(fields.size(), 15);
        QCOMPARE(fields.first().key, QStringLiteral("input_route"));
        QCOMPARE(fields.first().offset, 4);
        const GlobalField *midi = nullptr;
        for (const GlobalField &field : fields) {
            if (field.key == QStringLiteral("midi_channel"))
                midi = &field;
        }
        QVERIFY(midi != nullptr);
        QCOMPARE(midi->offset, 15);
        QCOMPARE(midi->displayOffset, 1);
    }

    void sysExAssemblerIgnoresRealtime()
    {
        QByteArray buffer;
        buffer.append(char(0xf8));
        buffer.append(QByteArray::fromHex("f042500106557301010003000100f7"));
        buffer.insert(3, char(0xf8));
        buffer.insert(7, char(0xfe));
        buffer.append(char(0xf8));
        const QByteArray frame = takeSysExFrame(&buffer);
        QCOMPARE(frame, QByteArray::fromHex("f042500106557301010003000100f7"));
        QCOMPARE(takeSysExFrame(&buffer), QByteArray());
        QVERIFY(buffer.isEmpty());
    }

    void midiBackendPreferences()
    {
        QCOMPARE(midiBackendPreferenceFromString(QStringLiteral("auto")),
                 MidiBackendPreference::Auto);
        QCOMPARE(midiBackendPreferenceFromString(QStringLiteral("pipewire")),
                 MidiBackendPreference::PipeWire);
        QCOMPARE(midiBackendPreferenceFromString(QStringLiteral("alsa")),
                 MidiBackendPreference::AlsaRaw);
        QCOMPARE(midiBackendPreferenceToString(MidiBackendPreference::PipeWire),
                 QStringLiteral("pipewire"));
        QCOMPARE(midiBackendPreferenceToString(MidiBackendPreference::AlsaRaw),
                 QStringLiteral("alsa"));
    }

    void midiPortDescriptions()
    {
        MidiPort raw;
        raw.card = 5;
        raw.device = 0;
        raw.subdevice = 1;
        raw.cardName = QStringLiteral("NTS-1 mkII");
        raw.portName = QStringLiteral("MIDI");
        QCOMPARE(raw.backendName(), QStringLiteral("ALSA raw"));
        QCOMPARE(raw.hwName(), QStringLiteral("hw:5,0,1"));

        MidiPort pw;
        pw.backend = MidiBackendType::PipeWireJack;
        pw.cardName = QStringLiteral("NTS-1 mkII");
        QCOMPARE(pw.backendName(), QStringLiteral("PipeWire"));
        QCOMPARE(pw.hwName(), QStringLiteral("pipewire"));
    }

    void workspaceRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("roundtrip.nts1mkii"));

        QByteArray programBytes(Program::kSize, '\0');
        programBytes.replace(0, 4, "PROG");
        programBytes.replace(Program::kSize - 4, 4, "PRED");
        Program program = Program::fromBytes(programBytes);
        program.setName(QStringLiteral("Roundtrip"));

        Workspace workspace;
        workspace.setCurrentProgram(program);
        workspace.library()[0] = program;
        workspace.global() = GlobalData::fromDeviceBytes(GlobalData::defaultDeviceData());
        workspace.global().setValue(15, 6);

        UserUnit unit;
        unit.bytes() = QByteArrayLiteral("not-a-real-elf-but-preserved");
        workspace.units().forModule(Module::Osc)[0] = unit;

        QString error;
        QVERIFY2(WorkspaceFile::write(path, &workspace, &error), qPrintable(error));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.startsWith(QByteArrayLiteral("NTS-1_MKII LIBRARY")));
        QCOMPARE(bytes.mid(40, 32),
                 QCryptographicHash::hash(bytes.mid(256), QCryptographicHash::Sha256));

        Workspace loaded;
        QVERIFY2(WorkspaceFile::read(path, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.currentProgram().name(), QStringLiteral("Roundtrip"));
        QCOMPARE(loaded.library().at(0).name(), QStringLiteral("Roundtrip"));
        QCOMPARE(loaded.global().value(15), 6);
        QCOMPARE(loaded.units().forModule(Module::Osc).at(0).bytes(),
                 unit.bytes());
    }
};

QTEST_MAIN(CoreTest)
#include "test_core.moc"
