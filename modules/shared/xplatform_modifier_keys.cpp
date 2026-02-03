// Modifier keys xplatform code.
#ifdef _WIN32
#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#include "windows.h"
#else
// TODO MAC>
#endif

#include "xplatform_modifier_keys.h"

namespace gmpi_deprecated
{
namespace modifier_keys
{
	bool isHeldShift()
	{
#ifdef _WIN32
		return GetKeyState(VK_SHIFT) < 0;
#else
		// TODO MAC>
        return false;
#endif
	}

	bool isHeldCtrl()
	{
#ifdef _WIN32
		return GetKeyState(VK_CONTROL) < 0;
#else
		// TODO MAC>
        return false;
#endif
	}

	bool isHeldAlt()
	{
#ifdef _WIN32
		return GetKeyState(VK_MENU) < 0;
#else
		// TODO MAC>
        return false;
#endif
	}

	int getHeldKeys()
	{
		int returnFlags = (int) Flags::None;

		if (isHeldShift())
			returnFlags |= (int)Flags::ShiftKey;

		if (isHeldCtrl())
			returnFlags |= (int)Flags::CtrlKey;

		if (isHeldAlt())
			returnFlags |= (int)Flags::AltKey;

		return returnFlags;
	}

}
}
