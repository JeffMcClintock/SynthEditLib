#include <algorithm>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/NativeUi.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;
using namespace gmpi::api;

class DialogGui final : public PluginEditor
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

		stockDialogUnknown = {};
		dialogHost->createStockDialog(static_cast<int32_t>(dialogType), stockDialogUnknown.put());
		if (!stockDialogUnknown)
			return;

		auto dlg = stockDialogUnknown.as<IStockDialog>();
		if (!dlg)
			return;

		dlg->setTitle(pinTitle.value.c_str());
		dlg->setText(pinMessage.value.c_str());

		dlg->showAsync(
			&bounds,
			new gmpi::sdk::StockDialogCallback(
				[this](StockDialogButton button)
				{
					pinResult = static_cast<int32_t>(button);
					stockDialogUnknown = {};
				}
			)
		);
	}

	ReturnCode render(drawing::api::IDeviceContext* dc) override
	{
		Graphics g(dc);

		const Rect r{0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top};
		auto brush = g.createSolidColorBrush(Colors::LightGray);
		g.fillRectangle(r, brush);

		brush.setColor(Colors::Gray);
		g.drawRectangle(r, brush);

		// draw label
		auto tf = g.getFactory().createTextFormat(12.0f);
		brush.setColor(Colors::Black);
		g.drawTextU("Click to open dialog", tf, r, brush);

		return ReturnCode::Ok;
	}

	ReturnCode onPointerDown(Point point, int32_t flags) override
	{
		if (flags & 1) // left button
		{
			onTrigger();
		}
		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<DialogGui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="SE: Dialog" name="Dialog" category="Sub-Controls" vendor="Jeff McClintock">
    <GUI graphicsApi="GmpiGui">
      <Pin name="Type" datatype="enum" default="0" metadata="Ok,Ok-Cancel,Yes-No,Yes-No-Cancel"/>
      <Pin name="Title" datatype="string" default="Dialog"/>
      <Pin name="Message" datatype="string" default="Are you sure?"/>
      <Pin name="Trigger" datatype="bool" direction="in"/>
      <Pin name="Result" datatype="enum" direction="out" metadata="Ok,Cancel,Yes,No"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}
