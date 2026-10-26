#pragma once

#include "core/model/Program.h"
#include "core/model/UserUnit.h"
#include "core/model/Workspace.h"

#include <QString>

namespace korg {

class WorkspaceFile
{
public:
    static bool read(const QString &path, Workspace *workspace, QString *error = nullptr);
    static bool write(const QString &path, Workspace *workspace, QString *error = nullptr);

    static bool readProgramFile(const QString &path, Program *program, QString *error = nullptr);
    static bool writeProgramFile(const QString &path, const Program &program, QString *error = nullptr);

    static bool readUnitFile(const QString &path, UserUnit *unit, QString *error = nullptr);
    static bool writeUnitFile(const QString &path, const UserUnit &unit, QString *error = nullptr);

    static QByteArray officialHeader();
};

} // namespace korg
