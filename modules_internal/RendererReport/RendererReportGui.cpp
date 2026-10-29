#include "mp_sdk_gui2.h"
#include "TimerManager.h"
#include "conversion.h"

#include <typeinfo>
#include "../se_sdk3_hosting/ModuleView.h"
#include "../se_sdk3_hosting/ViewBase.h"
#include "Shared/RendererInfo.h"

SE_DECLARE_INIT_STATIC_FILE(RendererReportGui);

using namespace gmpi;

// Asks the panel's drawing host for synthedit::IRendererInfo; hosts that wrap the frame forward it.
class RendererReportGui : public SeGuiInvisibleBase, public se_sdk::TimerClient
{
	BoolGuiPin pinRefresh;
	StringGuiPin pinReport;
	int pollsWithoutReport = 0;

	// The frame may not exist yet, and outputs set before the wiring is up are lost, so poll until a report arrives.
	void onRefresh()
	{
		if (pinRefresh)
		{
			pollsWithoutReport = 0;
			StartTimer(500);
		}
	}

	std::string readReport()
	{
		auto helper = dynamic_cast<SE2::Sdk3Helper*>(getHost());
		if (!helper)
			return getHost() ? std::string("Renderer: unknown, module host is ") + typeid(*getHost()).name() + ", not SE2::Sdk3Helper" : std::string{};

		auto& modview = helper->getModuleView();
		if (!modview.parent || !modview.parent->drawingHost)
			return {}; // not on a panel yet, keep polling.

		auto* host = modview.parent->drawingHost.get();
		if (const auto report = synthedit::queryRendererReport(host); !report.empty())
			return report;

		if (++pollsWithoutReport < 10)
			return {}; // the frame may not have built its swap chain yet.

		return std::string("Renderer: unknown, no host from ") + typeid(*host).name() + " to the frame offers synthedit::IRendererInfo";
	}

public:
	RendererReportGui()
	{
		initializePin(pinRefresh, static_cast<MpGuiBaseMemberPtr2>(&RendererReportGui::onRefresh));
		initializePin(pinReport);
		StartTimer(500);
	}

	bool OnTimer() override
	{
		const auto report = readReport();
		if (report.empty())
			return true; // keep polling.

		pinReport = Utf8ToWstring(report);
		return false;
	}
};

namespace
{
	auto r = sesdk::Register<RendererReportGui>::withXml(
R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="SE Renderer Report" name="Renderer Report" category="Diagnostic" >
    <GUI>
      <Pin name="Refresh" datatype="bool" />
      <Pin name="Report" datatype="string" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}
