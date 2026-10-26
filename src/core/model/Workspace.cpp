#include "core/model/Workspace.h"

namespace korg {

UnitLibrary::UnitLibrary()
    : osc(slotCount(Module::Osc))
    , modfx(slotCount(Module::ModFx))
    , delfx(slotCount(Module::DelFx))
    , revfx(slotCount(Module::RevFx))
{
}

QVector<UserUnit> &UnitLibrary::forModule(Module module)
{
    switch (module) {
    case Module::Osc:   return osc;
    case Module::ModFx: return modfx;
    case Module::DelFx: return delfx;
    case Module::RevFx: return revfx;
    }
    return osc;
}

const QVector<UserUnit> &UnitLibrary::forModule(Module module) const
{
    switch (module) {
    case Module::Osc:   return osc;
    case Module::ModFx: return modfx;
    case Module::DelFx: return delfx;
    case Module::RevFx: return revfx;
    }
    return osc;
}

void UnitLibrary::clear()
{
    osc.fill(UserUnit {});
    modfx.fill(UserUnit {});
    delfx.fill(UserUnit {});
    revfx.fill(UserUnit {});
}

Workspace::Workspace()
    : m_library(kLibrarySlots)
{
}

void Workspace::clear()
{
    m_currentProgram = Program();
    m_library.fill(Program());
    m_global = GlobalData();
    m_units.clear();
    m_modified = false;
    m_filePath.clear();
}

bool Workspace::isEmpty() const
{
    if (!m_currentProgram.isEmpty())
        return false;
    for (const Program &program : m_library) {
        if (!program.isEmpty())
            return false;
    }
    for (Module module : {Module::Osc, Module::ModFx, Module::DelFx, Module::RevFx}) {
        for (const UserUnit &unit : m_units.forModule(module)) {
            if (!unit.isEmpty())
                return false;
        }
    }
    return true;
}

void Workspace::setCurrentProgram(const Program &program)
{
    m_currentProgram = program;
}

int Workspace::firstFreeLibrarySlot() const
{
    for (int i = 0; i < m_library.size(); ++i) {
        if (m_library[i].isEmpty())
            return i;
    }
    return -1;
}

} // namespace korg
