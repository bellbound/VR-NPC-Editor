#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

// Windows defines GetObject as a macro, which collides with
// RE::BSScript::Variable::GetObject. Nothing here wants the macro.
#ifdef GetObject
#    undef GetObject
#endif

using namespace std::literals;
