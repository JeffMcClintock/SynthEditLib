#include <algorithm>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/NativeUi.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;
using namespace gmpi::api;

class DialogGui final : public PluginEditorNoGui
{
	Pin<int32_t> pinDialogType;   // 0=Ok, 1=OkCancel, 2=YesNo, 3=YesNoCancel
	Pin<std::string> pinTitle;
	Pin<std::string> pinMessage;
	Pin<bool> pinTrigger;         // pulse to open dialog
	Pin<int32_t> pinResult;       // 0=Ok, 1=Cancel, 2=Yes, 3=No

	gmpi::shared_ptr<IUnknown> stockDialogUnknown;

public:
	DialogGui()
	{
		pinTrigger.onUpdate = [this](PinBase*) { onTrigger(); };
	}

	void onTrigger()
	{
		if (!pinTrigger.value)
			return;

		if (!dialogHost)
			return;

		auto dialogType = static_cast<StockDialogType>(
			std::clamp(pinDialogType.value, 0, 3)
		);

		dialogHost->createStockDialog(
			static_cast<int32_t>(dialogType),
			pinTitle.value.c_str(),
			pinMessage.value.c_str(),
			stockDialogUnknown.put()
		);
		if (!stockDialogUnknown)
			return;

		auto dlg = stockDialogUnknown.as<IStockDialog>();
		if (!dlg)
			return;

		dlg->showAsync(
			new gmpi::sdk::StockDialogCallback(
				[this](StockDialogButton button)
				{
					pinResult = static_cast<int32_t>(button);
					stockDialogUnknown = {};
				}
			)
		);
	}
};

namespace
{
auto r = gmpi::Register<DialogGui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="SE: Dialog" name="OK Cancel Dialog" category="Sub-Controls" vendor="Jeff McClintock">
    <GUI graphicsApi="GmpiGui">
      <Pin name="Type" datatype="enum" metadata="Ok,Ok-Cancel,Yes-No,Yes-No-Cancel"/>
      <Pin name="Title" datatype="string" default="Dialog"/>
      <Pin name="Message" datatype="string" default="Are you sure?"/>
      <Pin name="Trigger" datatype="bool" direction="out"/>
      <Pin name="Result" datatype="int"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}
