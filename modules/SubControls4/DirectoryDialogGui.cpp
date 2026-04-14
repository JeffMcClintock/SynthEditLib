#include "helpers/GmpiPluginEditor.h"
#include "helpers/NativeUi.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::api;

class DirectoryDialogGui final : public PluginEditorNoGui
{
	Pin<std::string> pinFolderPath;
	Pin<std::string> pinInitialDirectory;
	Pin<bool> pinTrigger;

	gmpi::shared_ptr<IUnknown> fileDialogUnknown;

public:
	DirectoryDialogGui()
	{
		pinTrigger.onUpdate = [this](PinBase*) { onTrigger(); };
	}

	void onTrigger()
	{
		if (!pinTrigger.value)
			return;

		if (!dialogHost)
			return;

		dialogHost->createFileDialog(static_cast<int32_t>(FileDialogType::Folder), fileDialogUnknown.put());
		if (!fileDialogUnknown)
			return;

		auto dlg = fileDialogUnknown.as<IFileDialog>();
		if (!dlg)
			return;

		if (!pinInitialDirectory.value.empty())
			dlg->setInitialDirectory(pinInitialDirectory.value.c_str());

		dlg->showAsync(
			nullptr,
			new gmpi::sdk::FileDialogCallback(
				[this](const std::string& selectedPath)
				{
					pinFolderPath = selectedPath;
					fileDialogUnknown = {};
				}
			)
		);
	}
};

namespace
{
auto r = gmpi::Register<DirectoryDialogGui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="SE: DirectoryDialog" name="Directory Dialog" category="Sub-Controls" vendor="Jeff McClintock">
    <GUI graphicsApi="GmpiGui">
      <Pin name="Folder Path" datatype="string"/>
      <Pin name="Initial Directory" datatype="string"/>
      <Pin name="Trigger" datatype="bool" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}
