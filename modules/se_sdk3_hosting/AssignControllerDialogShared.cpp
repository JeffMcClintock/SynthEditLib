#include "AssignControllerDialogShared.h"

#include "../shared/it_enum_list.h"

namespace SE2
{

const std::vector<ControllerTypeInfo>& controllerTypeList()
{
	// Same entries, in the same order, as the editor's AssignControllerDialog.
	static const std::vector<ControllerTypeInfo> list =
	{
		{ ControllerType::None,            L"<none>" },
		{ ControllerType::CC,              L"CC" },
		{ ControllerType::RPN,             L"RPN" },
		{ ControllerType::NRPN,            L"NRPN" },
		{ ControllerType::SYSEX,           L"SYSEX" },
		{ ControllerType::Trigger,         L"Poly Trigger" },
		{ ControllerType::Gate,            L"Poly Gate" },
		{ ControllerType::Pitch,           L"Poly Pitch" },
		{ ControllerType::VelocityOn,      L"Poly Velocity Key On" },
		{ ControllerType::VelocityOff,     L"Poly Velocity Key Off" },
		{ ControllerType::PolyAftertouch,  L"Poly Aftertouch" },
		{ ControllerType::VirtualVoiceId,  L"Voice ID" },
		{ ControllerType::Bender,          L"Bender" },
		{ ControllerType::ChannelPressure, L"Channel Pressure" },
		{ ControllerType::GlideStartPitch, L"GlideStartPitch" },
	};
	return list;
}

const std::vector<CcNameEntry>& ccNameList()
{
	static const std::vector<CcNameEntry> list = []
	{
		std::vector<CcNameEntry> result;
		// NB: iterate via First()/Next(). it_enum_list's constructor does not prime the
		// first item (enum_entry default-inits index/value to 0, so IsDone() is already
		// false), so a plain `for(it(list); !IsDone(); ++it)` yields a phantom {value 0,
		// text ""} first — which used to shadow the real "0 - Bank Select" as CC 0.
		it_enum_list it(CONTROLLER_ENUM_LIST);
		for (it.First(); !it.IsDone(); it.Next())
		{
			if ((*it)->value < 0 || (*it)->value > 127)
				continue;

			result.push_back({ std::wstring((*it)->text.c_str()), (*it)->value });
		}
		return result;
	}();
	return list;
}

} // namespace SE2
