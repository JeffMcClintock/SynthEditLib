#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "modules/tinyXml2/tinyxml2.h"

/*
The multi-window arrangement of the editor, saved with the document so reopening a
project brings back the windows it was saved with: which tabs had been torn out into
their own window, where each window sat, and on which monitor.

Stored in the .synthedit document (CSynthEditDocBase::m_windowLayout) rather than in
ApplicationSettings, because tab membership is inherently document state - it is a list
of this document's containers - and the document already carries the neighbouring facts
(CContainer::ViewOpenFlags says which views are open, SelectedViewHandle/Type says which
one was in front).

EVERY front end serializes this; only the Windows one acts on it. That asymmetry is
deliberate. A front end with no torn-out windows (macOS, Wayland) opens every tab in its
main window and leaves the block untouched, so saving there round-trips a Windows layout
instead of silently discarding it.

Header-only on purpose: adding a .cpp would mean touching both SynthEditLib's CMakeLists
and SynthEdit2's vcxproj, and a file compiled into both targets is exactly the trap
described in the "don't compile a .cpp in two build targets" rule.
*/

// One tab: a view of one container. Identified the same way the document already
// identifies the selected tab (SelectedViewHandle / SelectedViewType), so it survives
// a save/load round trip and stays valid if modules are added or removed around it.
struct SavedEditorTab
{
	int32_t containerHandle = 0;
	int32_t viewType = 0; // CF_STRUCTURE_VIEW / CF_PANEL_VIEW
};

struct SavedEditorWindow
{
	// Virtual-screen pixels as the platform reported them at save time. Signed:
	// a monitor left of the primary one has negative coordinates.
	int32_t left = 0, top = 0, right = 0, bottom = 0;

	// The monitor this window was on. Both forms are recorded because each survives
	// a different kind of change: the device name survives a resolution change or the
	// monitors being rearranged, and the rect identifies the monitor when the device
	// name has changed (a different port, a docking station, a driver update).
	std::string monitorId;
	int32_t monitorLeft = 0, monitorTop = 0, monitorRight = 0, monitorBottom = 0;

	// Empty for the main window - its tabs are whatever is left over.
	std::vector<SavedEditorTab> tabs;

	bool hasPlacement() const { return right > left && bottom > top; }
};

struct EditorWindowLayout
{
	SavedEditorWindow mainWindow;
	std::vector<SavedEditorWindow> tearOutWindows;

	bool empty() const { return !mainWindow.hasPlacement() && tearOutWindows.empty(); }
	void clear() { mainWindow = {}; tearOutWindows.clear(); }

	// <WindowLayout>
	//   <MainWindow l=".." t=".." r=".." b=".." monitor="\\.\DISPLAY1" ml=".." .. />
	//   <TearOutWindow l=".." ...>
	//     <Tab handle="12345" view="1"/>
	//   </TearOutWindow>
	// </WindowLayout>
	void Export(tinyxml2::XMLElement* parent) const
	{
		if (empty())
			return;

		auto* doc = parent->GetDocument();
		auto* root = doc->NewElement("WindowLayout");
		parent->LinkEndChild(root);

		if (mainWindow.hasPlacement())
		{
			auto* e = doc->NewElement("MainWindow");
			root->LinkEndChild(e);
			ExportWindow(mainWindow, e);
		}

		for (const auto& w : tearOutWindows)
		{
			auto* e = doc->NewElement("TearOutWindow");
			root->LinkEndChild(e);
			ExportWindow(w, e);

			for (const auto& t : w.tabs)
			{
				auto* te = doc->NewElement("Tab");
				e->LinkEndChild(te);
				te->SetAttribute("handle", t.containerHandle);
				te->SetAttribute("view", t.viewType);
			}
		}
	}

	void Import(tinyxml2::XMLElement* parent)
	{
		clear();

		auto* root = parent->FirstChildElement("WindowLayout");
		if (!root)
			return;

		if (auto* e = root->FirstChildElement("MainWindow"))
			ImportWindow(e, mainWindow);

		for (auto* e = root->FirstChildElement("TearOutWindow"); e; e = e->NextSiblingElement("TearOutWindow"))
		{
			SavedEditorWindow w;
			ImportWindow(e, w);

			for (auto* te = e->FirstChildElement("Tab"); te; te = te->NextSiblingElement("Tab"))
			{
				SavedEditorTab t;
				te->QueryIntAttribute("handle", &t.containerHandle);
				te->QueryIntAttribute("view", &t.viewType);
				if (t.containerHandle != 0)
					w.tabs.push_back(t);
			}

			// A window with no tabs left (every container it held was deleted) would
			// restore as an empty frame, so drop it here rather than at restore time.
			if (!w.tabs.empty())
				tearOutWindows.push_back(w);
		}
	}

private:
	static void ExportWindow(const SavedEditorWindow& w, tinyxml2::XMLElement* e)
	{
		e->SetAttribute("l", w.left);
		e->SetAttribute("t", w.top);
		e->SetAttribute("r", w.right);
		e->SetAttribute("b", w.bottom);
		if (!w.monitorId.empty())
			e->SetAttribute("monitor", w.monitorId.c_str());
		e->SetAttribute("ml", w.monitorLeft);
		e->SetAttribute("mt", w.monitorTop);
		e->SetAttribute("mr", w.monitorRight);
		e->SetAttribute("mb", w.monitorBottom);
	}

	static void ImportWindow(tinyxml2::XMLElement* e, SavedEditorWindow& w)
	{
		e->QueryIntAttribute("l", &w.left);
		e->QueryIntAttribute("t", &w.top);
		e->QueryIntAttribute("r", &w.right);
		e->QueryIntAttribute("b", &w.bottom);

		const char* id{};
		if (tinyxml2::XML_SUCCESS == e->QueryStringAttribute("monitor", &id) && id)
			w.monitorId = id;

		e->QueryIntAttribute("ml", &w.monitorLeft);
		e->QueryIntAttribute("mt", &w.monitorTop);
		e->QueryIntAttribute("mr", &w.monitorRight);
		e->QueryIntAttribute("mb", &w.monitorBottom);
	}
};
