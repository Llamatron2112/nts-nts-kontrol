#include "core/file/WorkspaceFile.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSaveFile>

#include <cstring>

#include "miniz.h"

namespace korg {

namespace {

constexpr int kOfficialHeaderSize = 256;
constexpr int kRecordHeaderSize = 28;
constexpr int kRecordProgramOffset = 28;
constexpr int kRecordExtraSize = 132;
constexpr int kRecordSize = kRecordHeaderSize + Program::kSize + kRecordExtraSize;
constexpr int kRecordCount = 1 + Workspace::kLibrarySlots;
constexpr int kProgramEntrySize = kRecordCount * kRecordSize;

constexpr quint32 kRecordMagic = 0x42048330; // bytes 30 83 04 42 (little endian)

quint32 readLe32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return quint32(quint8(data[offset]))
            | (quint32(quint8(data[offset + 1])) << 8)
            | (quint32(quint8(data[offset + 2])) << 16)
            | (quint32(quint8(data[offset + 3])) << 24);
}

void writeLe32(QByteArray &data, int offset, quint32 value)
{
    if (offset < 0 || offset + 4 > data.size())
        return;
    data[offset] = char(value & 0xff);
    data[offset + 1] = char((value >> 8) & 0xff);
    data[offset + 2] = char((value >> 16) & 0xff);
    data[offset + 3] = char((value >> 24) & 0xff);
}

void appendLe16(QByteArray &data, quint16 value)
{
    data.append(char(value & 0xff));
    data.append(char((value >> 8) & 0xff));
}

void appendLe32(QByteArray &data, quint32 value)
{
    data.append(char(value & 0xff));
    data.append(char((value >> 8) & 0xff));
    data.append(char((value >> 16) & 0xff));
    data.append(char((value >> 24) & 0xff));
}

struct ZipBuildEntry {
    QByteArray name;
    QByteArray data;
};

QByteArray entryName(Module module, int slot)
{
    return QStringLiteral("%1%2").arg(moduleName(module)).arg(slot, 2, 10, QLatin1Char('0')).toLatin1();
}

bool extractEntry(mz_zip_archive *zip, const QByteArray &name, QByteArray *out)
{
    const int index = mz_zip_reader_locate_file(zip, name.constData(), nullptr, 0);
    if (index < 0)
        return false;
    size_t size = 0;
    void *data = mz_zip_reader_extract_to_heap(zip, mz_uint(index), &size, 0);
    if (!data) {
        if (size == 0) {
            *out = QByteArray();
            return true;
        }
        return false;
    }
    *out = QByteArray(static_cast<const char *>(data), int(size));
    mz_free(data);
    return true;
}

bool parseProgramEntry(const QByteArray &entry, Workspace *workspace, QString *error)
{
    if (entry.size() < kProgramEntrySize) {
        if (error)
            *error = QStringLiteral("The workspace program entry is too small.");
        return false;
    }

    for (int record = 0; record < kRecordCount; ++record) {
        const int base = record * kRecordSize;
        const quint32 magic = readLe32(entry, base);
        if (magic != kRecordMagic)
            continue;
        const quint32 used = readLe32(entry, base + 8);
        const QByteArray programBytes = entry.mid(base + kRecordProgramOffset, Program::kSize);
        if (used == 0 || !Program::looksValid(programBytes))
            continue;
        const Program program = Program::fromBytes(programBytes);
        if (record == 0)
            workspace->setCurrentProgram(program);
        else
            workspace->library()[record - 1] = program;
    }
    return true;
}

QByteArray buildProgramEntry(const Workspace &workspace)
{
    QByteArray entry(kProgramEntrySize, '\0');
    for (int record = 0; record < kRecordCount; ++record) {
        const int base = record * kRecordSize;
        writeLe32(entry, base, kRecordMagic);
        writeLe32(entry, base + 4, 1);
        const Program *program = nullptr;
        if (record == 0) {
            if (workspace.currentProgram().isValid())
                program = &workspace.currentProgram();
        } else {
            const Program &candidate = workspace.library().at(record - 1);
            if (candidate.isValid())
                program = &candidate;
        }

        if (program) {
            writeLe32(entry, base + 8, 2);
            entry.replace(base + kRecordProgramOffset, Program::kSize, program->bytes());
        } else {
            writeLe32(entry, base + 8, 0);
            for (int i = 0; i < 4; ++i)
                writeLe32(entry, base + 12 + i * 4, 2);
        }
    }
    return entry;
}

bool readZip(const QByteArray &zipData, Workspace *workspace, QString *error)
{
    mz_zip_archive zip {};
    if (!mz_zip_reader_init_mem(&zip, zipData.constData(), size_t(zipData.size()), 0)) {
        if (error)
            *error = QStringLiteral("The workspace ZIP archive is invalid.");
        return false;
    }

    QByteArray entry;
    if (extractEntry(&zip, "program", &entry))
        parseProgramEntry(entry, workspace, error);

    if (extractEntry(&zip, "global", &entry))
        workspace->global().setWorkspaceBytes(entry);
    if (extractEntry(&zip, "global_raw", &entry)
        && GlobalData::looksLikeDeviceData(entry)) {
        workspace->global() = GlobalData::fromDeviceBytes(entry);
    }

    for (Module module : {Module::Osc, Module::ModFx, Module::DelFx, Module::RevFx}) {
        QVector<UserUnit> &units = workspace->units().forModule(module);
        for (int slot = 0; slot < units.size(); ++slot) {
            if (!extractEntry(&zip, entryName(module, slot), &entry))
                continue;
            if (entry.isEmpty()) {
                units[slot] = UserUnit();
                continue;
            }
            units[slot] = UserUnit::fromBytes(entry);
        }
    }

    mz_zip_reader_end(&zip);
    return true;
}

bool collectZip(const Workspace &workspace, QByteArray *zipData, QString *error)
{
    QList<ZipBuildEntry> entries;
    entries.append({QByteArrayLiteral("program"), buildProgramEntry(workspace)});
    for (Module module : {Module::Osc, Module::ModFx, Module::DelFx, Module::RevFx}) {
        const QVector<UserUnit> &units = workspace.units().forModule(module);
        for (int slot = 0; slot < units.size(); ++slot)
            entries.append({entryName(module, slot), units.at(slot).bytes()});
    }
    const QByteArray global = workspace.global().workspaceBytes().size() == GlobalData::kWorkspaceSize
                                  ? workspace.global().workspaceBytes()
                                  : GlobalData::defaultWorkspaceData();
    entries.append({QByteArrayLiteral("global"), global});
    if (workspace.global().isValid())
        entries.append({QByteArrayLiteral("global_raw"), workspace.global().deviceBytes()});

    struct CentralEntry {
        QByteArray name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };
    QList<CentralEntry> central;
    QByteArray zip;
    zip.reserve(400000);

    for (const ZipBuildEntry &entry : entries) {
        CentralEntry info;
        info.name = entry.name;
        info.crc = crc32(entry.data);
        info.size = quint32(entry.data.size());
        info.offset = quint32(zip.size());

        appendLe32(zip, 0x04034b50);
        appendLe16(zip, 10);          // version needed
        appendLe16(zip, 0);           // flags
        appendLe16(zip, 0);           // stored
        appendLe16(zip, 0);           // time
        appendLe16(zip, 0x4221);      // date (2013-01-01)
        appendLe32(zip, info.crc);
        appendLe32(zip, info.size);
        appendLe32(zip, info.size);
        appendLe16(zip, quint16(entry.name.size()));
        appendLe16(zip, 0);           // extra
        zip.append(entry.name);
        zip.append(entry.data);
        central.append(info);
    }

    const quint32 centralOffset = quint32(zip.size());
    for (const CentralEntry &info : central) {
        appendLe32(zip, 0x02014b50);
        appendLe16(zip, 20);          // version made by
        appendLe16(zip, 10);          // version needed
        appendLe16(zip, 0);           // flags
        appendLe16(zip, 0);           // stored
        appendLe16(zip, 0);           // time
        appendLe16(zip, 0x4221);      // date
        appendLe32(zip, info.crc);
        appendLe32(zip, info.size);
        appendLe32(zip, info.size);
        appendLe16(zip, quint16(info.name.size()));
        appendLe16(zip, 0);           // extra
        appendLe16(zip, 0);           // comment
        appendLe16(zip, 0);           // disk
        appendLe16(zip, 0);           // internal attributes
        appendLe32(zip, 0);           // external attributes
        appendLe32(zip, info.offset);
        zip.append(info.name);
    }
    const quint32 centralSize = quint32(zip.size()) - centralOffset;

    appendLe32(zip, 0x06054b50);
    appendLe16(zip, 0);
    appendLe16(zip, 0);
    appendLe16(zip, quint16(central.size()));
    appendLe16(zip, quint16(central.size()));
    appendLe32(zip, centralSize);
    appendLe32(zip, centralOffset);
    appendLe16(zip, 0);

    *zipData = zip;
    return true;
}

bool readFileBytes(const QString &path, QByteArray *data, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    *data = file.readAll();
    return true;
}

} // namespace

QByteArray WorkspaceFile::officialHeader()
{
    static const QByteArray header = QByteArray::fromHex(
        "4e54532d315f4d4b4949204c4942524152592020202020202020202020202000"
        "0100ffff000000003c9888c761d96b2eedef10b1ec8603bacc4bd5ebd384bb83"
        "320857ba2a2665f5ffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    return header;
}

bool WorkspaceFile::read(const QString &path, Workspace *workspace, QString *error)
{
    if (!workspace) {
        if (error)
            *error = QStringLiteral("No workspace destination.");
        return false;
    }

    QByteArray fileData;
    if (!readFileBytes(path, &fileData, error))
        return false;

    Workspace loaded;
    bool ok = false;
    if (fileData.size() > kOfficialHeaderSize
        && fileData.startsWith(QByteArrayLiteral("NTS-1_MKII LIBRARY"))) {
        QByteArray zipData = fileData.mid(kOfficialHeaderSize);
        if (!zipData.startsWith(QByteArrayLiteral("PK\x03\x04"))) {
            const int zipStart = fileData.indexOf(QByteArrayLiteral("PK\x03\x04"),
                                                   kOfficialHeaderSize);
            if (zipStart >= 0)
                zipData = fileData.mid(zipStart);
        }
        ok = readZip(zipData, &loaded, error);
    } else if (fileData.startsWith(QByteArrayLiteral("PK\x03\x04"))) {
        ok = readZip(fileData, &loaded, error);
    } else if (Program::looksValid(fileData)) {
        loaded.setCurrentProgram(Program::fromBytes(fileData));
        ok = true;
    } else if (error) {
        *error = QStringLiteral("This is not a NTS-1 mkII workspace file.");
    }

    if (!ok)
        return false;

    loaded.setFilePath(path);
    loaded.setModified(false);
    *workspace = loaded;
    return true;
}

bool WorkspaceFile::write(const QString &path, Workspace *workspace, QString *error)
{
    if (!workspace) {
        if (error)
            *error = QStringLiteral("No workspace source.");
        return false;
    }

    QByteArray zipData;
    if (!collectZip(*workspace, &zipData, error))
        return false;

    QByteArray header = officialHeader();
    const QByteArray digest = QCryptographicHash::hash(zipData, QCryptographicHash::Sha256);
    if (header.size() >= 72 && digest.size() == 32)
        header.replace(40, 32, digest);

    QByteArray output;
    output.reserve(kOfficialHeaderSize + zipData.size());
    output.append(header);
    output.append(zipData);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(output) != output.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }

    workspace->setFilePath(path);
    workspace->setModified(false);
    return true;
}

bool WorkspaceFile::readProgramFile(const QString &path, Program *program, QString *error)
{
    QByteArray data;
    if (!readFileBytes(path, &data, error))
        return false;

    if (Program::looksValid(data)) {
        *program = Program::fromBytes(data);
        return true;
    }

    const int prog = data.indexOf(QByteArrayLiteral("PROG"));
    if (prog >= 0 && prog + Program::kSize <= data.size()) {
        const QByteArray candidate = data.mid(prog, Program::kSize);
        if (Program::looksValid(candidate)) {
            *program = Program::fromBytes(candidate);
            return true;
        }
    }

    if (error)
        *error = QStringLiteral("This is not a valid NTS-1 mkII program file.");
    return false;
}

bool WorkspaceFile::writeProgramFile(const QString &path, const Program &program, QString *error)
{
    if (!program.isValid()) {
        if (error)
            *error = QStringLiteral("The program data is not valid.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(program.bytes()) != program.bytes().size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool WorkspaceFile::readUnitFile(const QString &path, UserUnit *unit, QString *error)
{
    QByteArray data;
    if (!readFileBytes(path, &data, error))
        return false;
    if (data.size() < 4 || !data.startsWith(QByteArrayLiteral("\x7f" "ELF"))) {
        if (error)
            *error = QStringLiteral("This is not an ELF user unit file.");
        return false;
    }
    *unit = UserUnit::fromBytes(data);
    return true;
}

bool WorkspaceFile::writeUnitFile(const QString &path, const UserUnit &unit, QString *error)
{
    if (unit.bytes().isEmpty()) {
        if (error)
            *error = QStringLiteral("The user unit is empty.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(unit.bytes()) != unit.bytes().size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace korg
