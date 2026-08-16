#pragma once
#include "IViewChild.h"
#include "ViewBase.h"
#include "helpers/Timer.h"
#include "helpers/NativeUi.h"
#include "ModuleFactory_Editor.h"
#include "GmpiSdkCommon.h"
#include "GmpiUiDrawing.h"
#include "gmpi_drawing_conversions.h"
#include "Pile.h"

using namespace legacy_converters;

namespace keyEditorLogic
{
struct State
{
	int timerCounter = 0;
	std::wstring text;
	int cursorPos{};
	int selectedFrom = -1;
	int selectedTo = -1;

	auto operator<=>(const State&) const = default;
};

inline State processKey(const State& inState, int32_t key, int32_t flags)
{
	State outState{ inState };

	switch (key)
	{
	case 0:
		break;

	case 0x25: // <LEFT>
		outState.cursorPos = (std::max)(outState.cursorPos - 1, 0);
		outState.selectedFrom = outState.cursorPos;

		if (!(flags & gmpi_gui_api::GG_POINTER_KEY_SHIFT))
			outState.selectedTo = outState.cursorPos;
		break;

	case 0x27: // <RIGHT>
		outState.cursorPos = (std::min)(outState.cursorPos + 1, (int)outState.text.size());
		outState.selectedTo = outState.cursorPos;

		if (!(flags & gmpi_gui_api::GG_POINTER_KEY_SHIFT))
			outState.selectedFrom = outState.cursorPos;
		break;

	case 0x2E: // <DEL>
		if (outState.selectedFrom == outState.selectedTo)
		{
			if (outState.cursorPos < outState.text.size())
			{
				outState.text.erase(outState.cursorPos, 1);
			}
		}
		else
		{
			outState.text.erase(outState.selectedFrom, outState.selectedTo - outState.selectedFrom);
			outState.cursorPos = outState.selectedFrom;
			outState.selectedTo = outState.selectedFrom;
		}
		break;

	case 0x08: // <BACKSPACE>
		if (outState.selectedFrom == outState.selectedTo)
		{
			if (outState.cursorPos > 0)
			{
				outState.text.erase(outState.cursorPos - 1, 1);
				outState.cursorPos--;
			}
		}
		else
		{
			outState.text.erase(outState.selectedFrom, outState.selectedTo - outState.selectedFrom);
			outState.cursorPos = outState.selectedFrom;
			outState.selectedTo = outState.selectedFrom;
		}
		break;

	default:
		if (key >= 0x20 /*&& key <= 0x7E*/)
		{
			outState.text.insert(outState.cursorPos, 1, (wchar_t)key);
			outState.cursorPos++;
		}
		break;
	}

	return outState;
}

inline bool cursorBlinkState(const State& inState)
{
	return (inState.timerCounter & 0x8) == 0; // approx 2Hz blink.
}

} // namespace

struct moduleInfo4List
{
	std::wstring module_type;
	std::string displayName;
};

struct ModulePicker2 :
	  public gmpi::api::IDrawingClient
	, public gmpi::api::IInputClient
	, public gmpi::api::IKeyListenerCallback
	, public gmpi::TimerClient
{
	gmpi::shared_ptr<gmpi::api::IDrawingHost> drawingHost;
	gmpi::shared_ptr<gmpi::api::IInputHost> inputHost;
	gmpi::shared_ptr<gmpi::api::IDialogHost> dialogHost;

	SE2::IPresenter* presenter{};
	gmpi::drawing::Rect bounds_;
	gmpi::shared_ptr<gmpi::api::IKeyListener> listener;
	keyEditorLogic::State state;
	std::multimap<std::wstring, menuinfo> moduleNames;
	std::vector< moduleInfo4List> moduleListData;

	std::wstring glyfSource;
	std::vector<float> glyfBounds;
	float cursorHeight{};
	std::wstring filterText{ L"none" };

	int hoveredRow{ -1 };

	ModulePicker2(SE2::IPresenter* p)
		: presenter(p)
	{
	}

	// call with 62ms timer plse.
	bool onTimer() override
	{
		const auto blinkBefore = cursorBlinkState(state);

		++state.timerCounter;

		if (blinkBefore != cursorBlinkState(state)) // cursor state changed?
		{
			drawingHost->invalidateRect(&bounds_);
		}

		return true;
	}

	// IDrawingClient + IInputClient (single setHost satisfies both via MI)
	gmpi::ReturnCode setHost(gmpi::api::IUnknown* host) override
	{
		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		unknown = (gmpi::api::IUnknown*)host;

		inputHost = unknown.as<gmpi::api::IInputHost>();
		drawingHost = unknown.as<gmpi::api::IDrawingHost>();
		dialogHost = unknown.as<gmpi::api::IDialogHost>();

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
	{
		(void)availableSize;
		returnDesiredSize->width = 160.0f;
		returnDesiredSize->height = 160.0f;

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override
	{
		bounds_ = *finalRect;
		init();

		return gmpi::ReturnCode::Ok;
	}

	void updateFilter(std::wstring newfilterText)
	{
		std::transform(newfilterText.begin(), newfilterText.end(), newfilterText.begin(),
			[](wchar_t c) { return static_cast<wchar_t>(std::tolower(c)); });

		if (newfilterText == filterText) // avoid expensive filtering if we are mearly moving the cursor arround etc.
			return;

		filterText = newfilterText;

		moduleListData.clear();
		for (const auto& [keystring, info] : moduleNames)
		{
			if (keystring.find(filterText) != std::string::npos)
			{
				moduleListData.push_back({ info.unique_id, WStringToUtf8(info.name) });
			}
		}
	}

	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		gmpi::drawing::Graphics g(drawingContext);

		const gmpi::drawing::Rect r{ 0, 0, getWidth(bounds_), getHeight(bounds_) }; // TODO !!!! bounds should be relative this this, not my parent

		// render text-entry box
		{
			auto textFormat = g.getFactory().createTextFormat(12.0f);

			// calculate the width of each character in the string, so as to know where to draw the cursor.
			if (glyfSource != state.text || glyfBounds.empty())
			{
				glyfSource = state.text;
				glyfBounds.clear();

				std::wstring wide;

				glyfBounds.push_back(0.0f);
				for (auto c : glyfSource)
				{
					wide += c;
					auto utf8 = WStringToUtf8(wide);
					auto bounds = textFormat.getTextExtentU(utf8);
					glyfBounds.push_back(bounds.width);
				}
			}

			auto brush = g.createSolidColorBrush(gmpi::drawing::Color{ 0, 0, 0, 0.75f });
			g.fillRectangle(r, brush);

			// highlight background of currently selected text
			if (state.selectedFrom != state.selectedTo)
			{
				brush.setColor(gmpi::drawing::Color(1, 1, 1, 0.3f));
				auto bounds = textFormat.getTextExtentU(WStringToUtf8(state.text.substr(0, state.selectedFrom)));
				auto bounds2 = textFormat.getTextExtentU(WStringToUtf8(state.text.substr(0, state.selectedTo)));
				g.fillRectangle({ bounds.width, 0, bounds2.width, bounds.height }, brush);
			}

			brush.setColor(gmpi::drawing::Color(1, 1, 1, 1));

			// draw text
			g.drawTextU(WStringToUtf8(state.text), textFormat, r, brush);

			// draw blinking cursor
			if (cursorBlinkState(state))
			{
				if (!cursorHeight)
				{
					auto bounds = textFormat.getTextExtentU("|");
					cursorHeight = bounds.height;
				}

				const auto cursorX = glyfBounds[std::clamp(state.cursorPos, 0, -1 + (int)glyfBounds.size())];
				g.drawLine(gmpi::drawing::Point(cursorX, 0), gmpi::drawing::Point(cursorX, cursorHeight), brush, 1.0f);
			}
		}

		// render filtered modulelist
		{
			auto textFormat = g.getFactory().createTextFormat(12.0f);
			auto brush = g.createSolidColorBrush(gmpi::drawing::Color(1, 1, 1, 1));

			gmpi::drawing::Rect textRect{
				0,
				12,
				getWidth(bounds_),
				24
			};

			int row{};
			for (auto& info : moduleListData)
			{
				if (hoveredRow == row)
				{
					auto b = g.createSolidColorBrush(gmpi::drawing::Colors::Orange);
					g.fillRectangle(textRect, b);
				}

				g.drawTextU(info.displayName, textFormat, textRect, brush);

				textRect.top = textRect.bottom;
				textRect.bottom += 12;

				if (textRect.bottom > getHeight(bounds_))
					break;

				++row;
			}
		}

		return gmpi::ReturnCode::Ok;
	}

	// IInputClient : gmpi::api::IUnknown
	// Mouse events.
	gmpi::ReturnCode setHover(bool isMouseOverMe) override { return gmpi::ReturnCode::Ok;}
	gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override { return gmpi::ReturnCode::Ok;}
	gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
	{
		//		const auto localPoint = gmpi::drawing::Point(point.x, point.y) - gmpi::drawing::Point(bounds_.left, bounds_.top);
		//		hoveredRow = std::clamp(static_cast<int>(localPoint.height / 12) - 1, -1, (int)moduleListData.size() - 1);
		//		_RPTN(0, "clicked %s\n", hoveredRow < 0 ? "none" : moduleListData[hoveredRow].displayName.c_str());

		if (hoveredRow >= 0 && hoveredRow < moduleListData.size())
		{
			const auto moduleType = moduleListData[hoveredRow].module_type;
			presenter->AddModule(moduleType.c_str(), point);

			closeMe();
			//const auto safeParent = parent; // I'm about to be deleted.
			//safeParent->DismissModulePicker();
			//safeParent->Presenter()->AddModule(moduleType.c_str(), point);
		}

		return gmpi::ReturnCode::Ok;
	}
	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
	{
//		const auto localPoint = gmpi::drawing::Point(point.x, point.y) - gmpi::drawing::Point(bounds_.left, bounds_.top);
		//		const auto prev = hoveredRow;

		hoveredRow = std::clamp(static_cast<int>(point.y / 12) - 1, -1, (int)moduleListData.size() - 1);

		//if (prev != hoveredRow)
		//{
		//	_RPTN(0, "hover %d\n", hoveredRow);
		//}

		return gmpi::ReturnCode::Ok;
	}
	gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override { return gmpi::ReturnCode::Ok;}
	gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override { return gmpi::ReturnCode::Ok;}
	gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override { return gmpi::ReturnCode::Ok;}
//	gmpi::ReturnCode onContextMenu(int32_t idx) override { return gmpi::ReturnCode::Ok;}
	gmpi::ReturnCode onKeyPress(wchar_t c) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode getToolTip(gmpi::drawing::Point point, gmpi::api::IString* returnString) override { return gmpi::ReturnCode::Unhandled; }

	gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override
	{
		*returnRect = bounds_;
		return gmpi::ReturnCode::Ok;
	}

	void init()
	{
		if (!listener.isNull())
			return;

		dialogHost->createKeyListener((const gmpi::drawing::Rect*)&bounds_, (gmpi::api::IUnknown**)listener.put());

		assert(listener);

		listener->showAsync(static_cast<gmpi::api::IKeyListenerCallback*>(this));

		moduleNames = ExportModuleNames();
		updateFilter({});

		startTimer(62);
	}

	// IKeyListenerCallback
	void onKeyDown(int32_t key, int32_t flags) override
	{
		auto newState = processKey(state, key, flags);

		if (newState != state)
		{
			state = newState;
			state.timerCounter = 0; // blink cursor ON
			updateFilter(state.text);
			drawingHost->invalidateRect(&bounds_);
		}
	}
	void onKeyUp(int32_t key, int32_t flags) override
	{
#ifdef _WIN32
		//		_RPTWN(0, L"key up %c\n", (wchar_t)key);
#endif
		if (key == 0x1B) // <ESC> to cancel
		{
			closeMe();
		}
		else if (key == 0x0D || key == L'\t') // <ENTER> or <TAB> to select
		{
			closeMe();
		}
	}
	void onLostFocus(gmpi::ReturnCode /*result*/) override
	{
		closeMe();
	}
	void cut(gmpi::api::IString* returnString) override {}
	void copy(gmpi::api::IString* returnString) override {}
	void paste(const char* text, size_t size) override {}

	void closeMe()
	{
		stopTimer();
		listener = nullptr; // release it.

		dynamic_cast<SE2::GmpiUiLayerHost*>(drawingHost.get())->owner->removeChild(static_cast<gmpi::api::IDrawingClient*>(this));
	}

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		GMPI_QUERYINTERFACE(IInputClient);
		GMPI_QUERYINTERFACE(IDrawingClient);
		GMPI_QUERYINTERFACE(gmpi::api::IKeyListenerCallback);
		return gmpi::ReturnCode::NoSupport;
	}

	GMPI_REFCOUNT
};


class ModulePicker :
	  public SE2::ViewChild
	, public gmpi::api::IKeyListenerCallback
	, public gmpi::TimerClient
{
	gmpi::shared_ptr<gmpi::api::IKeyListener> listener;
	keyEditorLogic::State state;
	std::multimap<std::wstring, menuinfo> moduleNames;
	std::vector< moduleInfo4List> moduleListData;

	std::wstring glyfSource;
	std::vector<float> glyfBounds;
	float cursorHeight{};
	std::wstring filterText{ L"none" };

	int hoveredRow{ -1 };

	// call with 62ms timer plse.
	bool onTimer() override
	{
		const auto blinkBefore = cursorBlinkState(state);

		++state.timerCounter;

		if (blinkBefore != cursorBlinkState(state)) // cursor state changed?
		{
			parent->invalidateRect(&bounds_);
		}

		return true;
	}

public:
	ModulePicker(SE2::ViewBase* pParent) :
		ViewChild(pParent, -1)
	{
	}

	~ModulePicker() = default;

	void arrange(gmpi::drawing::Rect finalRect) override
	{
		SE2::ViewChild::arrange(finalRect);

		init();
	}

	void updateFilter(std::wstring newfilterText)
	{
		std::transform(newfilterText.begin(), newfilterText.end(), newfilterText.begin(),
			[](wchar_t c) { return static_cast<wchar_t>(std::tolower(c)); });

		if (newfilterText == filterText) // avoid expensive filtering if we are mearly moving the cursor arround etc.
			return;

		filterText = newfilterText;

		moduleListData.clear();
		for (const auto& [keystring, info] : moduleNames)
		{
			if (keystring.find(filterText) != std::string::npos)
			{
				moduleListData.push_back({ info.unique_id, WStringToUtf8(info.name) });
			}
		}
	}

	void render(gmpi::drawing::Graphics& g) override
	{
		// render text-entry box
		{
			auto textFormat = g.getFactory().createTextFormat(12.0f);

			// calculate the width of each character in the string, so as to know where to draw the cursor.
			if (glyfSource != state.text || glyfBounds.empty())
			{
				glyfSource = state.text;
				glyfBounds.clear();

				std::wstring wide;

				glyfBounds.push_back(0.0f);
				for (auto c : glyfSource)
				{
					wide += c;
					auto utf8 = WStringToUtf8(wide);
					auto bounds = textFormat.getTextExtentU(utf8);
					glyfBounds.push_back(bounds.width);
				}
			}

			auto brush = g.createSolidColorBrush(gmpi::drawing::Color(0, 0, 0, 0.75f));
			gmpi::drawing::Rect r(0, 0, getWidth(bounds_), getHeight(bounds_));
			g.fillRectangle(r, brush);

			// highlight background of currently selected text
			if (state.selectedFrom != state.selectedTo)
			{
				brush.setColor(gmpi::drawing::Color(1, 1, 1, 0.3f));
				auto bounds = textFormat.getTextExtentU(WStringToUtf8(state.text.substr(0, state.selectedFrom)));
				auto bounds2 = textFormat.getTextExtentU(WStringToUtf8(state.text.substr(0, state.selectedTo)));
				g.fillRectangle(gmpi::drawing::Rect(bounds.width, 0, bounds2.width, bounds.height), brush);
			}

			brush.setColor(gmpi::drawing::Color(1, 1, 1, 1));

			// draw text
			g.drawTextU(WStringToUtf8(state.text).c_str(), textFormat, r, brush);

			// draw blinking cursor
			if (cursorBlinkState(state))
			{
				if (!cursorHeight)
				{
					auto bounds = textFormat.getTextExtentU("|");
					cursorHeight = bounds.height;
				}

				const auto cursorX = glyfBounds[std::clamp(state.cursorPos, 0, -1 + (int)glyfBounds.size())];
				g.drawLine(gmpi::drawing::Point(cursorX, 0), gmpi::drawing::Point(cursorX, cursorHeight), brush, 1.0f);
			}
		}

		// render filtered modulelist
		{
			auto textFormat = g.getFactory().createTextFormat(12.0f);
			auto brush = g.createSolidColorBrush(gmpi::drawing::Color(1, 1, 1, 1));

			gmpi::drawing::Rect textRect{
				0,
				12,
				getWidth(bounds_),
				24
			};

			int row{};
			for (auto& info : moduleListData)
			{
				if (hoveredRow == row)
				{
					auto highlightBrush = g.createSolidColorBrush(gmpi::drawing::Colors::Orange);
					g.fillRectangle(textRect, highlightBrush);
				}

				g.drawTextU(info.displayName.c_str(), textFormat, textRect, brush);

				textRect.top = textRect.bottom;
				textRect.bottom += 12;

				if (textRect.bottom > getHeight(bounds_))
					break;

				++row;
			}
		}
	}

	gmpi::ReturnCode setHover(bool isMouseOverMe) override
	{
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
	{
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
	{
//		const auto localPoint = gmpi::drawing::Point(point.x, point.y) - gmpi::drawing::Point(bounds_.left, bounds_.top);
//		hoveredRow = std::clamp(static_cast<int>(localPoint.height / 12) - 1, -1, (int)moduleListData.size() - 1);

//		_RPTN(0, "clicked %s\n", hoveredRow < 0 ? "none" : moduleListData[hoveredRow].displayName.c_str());

		if (hoveredRow >= 0 && hoveredRow < moduleListData.size())
		{
			const auto safeParent = parent; // I'm about to be deleted.
			const auto moduleType = moduleListData[hoveredRow].module_type;
			safeParent->DismissModulePicker();
			safeParent->Presenter()->AddModule(moduleType.c_str(), point);
		}

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
	{
		const auto localPoint = gmpi::drawing::Point(point.x - bounds_.left, point.y - bounds_.top);// -gmpi::drawing::Point(, bounds_.top);
//		const auto prev = hoveredRow;

		hoveredRow = std::clamp(static_cast<int>(localPoint.y / 12) - 1, -1, (int)moduleListData.size() - 1);

		//if (prev != hoveredRow)
		//{
		//	_RPTN(0, "hover %d\n", hoveredRow);
		//}

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
	{
		/* ?
		parent->releaseCapture();
		parent->RemoveChild(this);
		delete this;
		*/
		return gmpi::ReturnCode::Unhandled;
	}
	gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override { return gmpi::ReturnCode::Unhandled; }
	gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override { return gmpi::ReturnCode::Unhandled; }
	gmpi::ReturnCode onContextMenu(int32_t idx) override { return gmpi::ReturnCode::Unhandled; }
	gmpi::ReturnCode onKeyPress(wchar_t c) override { return gmpi::ReturnCode::Unhandled; }

	void OnMoved(gmpi::drawing::Rect& newRect) override {}
	void OnNodesMoved(std::vector<gmpi::drawing::Point>& newNodes) override {}

	void init()
	{
		if (!listener.isNull())
			return;

		moduleNames = ExportModuleNames();
		updateFilter({});

		startTimer(62);
	}

	// IKeyListenerCallback
	void onKeyDown(int32_t key, int32_t flags) override
	{
		auto newState = processKey(state, key, flags);

		if (newState != state)
		{
			state = newState;
			state.timerCounter = 0; // blink cursor ON
			updateFilter(state.text);
			parent->invalidateRect(&bounds_);
		}
	}

	void onKeyUp(int32_t key, int32_t flags) override
	{
#ifdef _WIN32
//		_RPTWN(0, L"key up %c\n", (wchar_t)key);
#endif
		if (key == 0x1B) // <ESC> to cancel
		{
			parent->DismissModulePicker();
		}
		else if (key == 0x0D || key == L'\t') // <ENTER> or <TAB> to select
		{
			parent->DismissModulePicker();
		}
	}
	void onLostFocus(gmpi::ReturnCode /*result*/) override
	{
		listener = nullptr; // release it.
		parent->DismissModulePicker();
	}
	void cut(gmpi::api::IString* returnString) override
	{
	}
	void copy(gmpi::api::IString* returnString) override
	{
	}
	void paste(const char* text, size_t size) override
	{
	}

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		*returnInterface = {};
		GMPI_QUERYINTERFACE(gmpi::api::IKeyListenerCallback);
		return gmpi::ReturnCode::NoSupport;
	}
	GMPI_REFCOUNT_NO_DELETE;
};

