#include "IGuiHost2.h"

// The one definition, in the library that declares it: the VST3 wrapper writes it and
// se_gmpi's editor reads it, but neither is linked into every target that needs it.
IGuiHost2* g_pendingEditorGuiHost = {};
