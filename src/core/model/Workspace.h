#pragma once

#include "core/model/GlobalData.h"
#include "core/model/Program.h"
#include "core/model/UserUnit.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QVector>

namespace korg {

struct UnitLibrary {
    QVector<UserUnit> osc;
    QVector<UserUnit> modfx;
    QVector<UserUnit> delfx;
    QVector<UserUnit> revfx;

    UnitLibrary();

    QVector<UserUnit> &forModule(Module module);
    const QVector<UserUnit> &forModule(Module module) const;
    void clear();
};

class Workspace
{
public:
    static constexpr int kLibrarySlots = 100;

    Workspace();

    void clear();
    bool isEmpty() const;

    Program &currentProgram() { return m_currentProgram; }
    const Program &currentProgram() const { return m_currentProgram; }
    void setCurrentProgram(const Program &program);

    QVector<Program> &library() { return m_library; }
    const QVector<Program> &library() const { return m_library; }
    int firstFreeLibrarySlot() const;

    GlobalData &global() { return m_global; }
    const GlobalData &global() const { return m_global; }

    UnitLibrary &units() { return m_units; }
    const UnitLibrary &units() const { return m_units; }

    bool modified() const { return m_modified; }
    void setModified(bool modified) { m_modified = modified; }

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

private:
    Program m_currentProgram;
    QVector<Program> m_library;
    GlobalData m_global;
    UnitLibrary m_units;
    bool m_modified = false;
    QString m_filePath;
};

} // namespace korg
