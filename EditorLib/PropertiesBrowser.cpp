// Compiled into EditorLib so both SynthEdit2 (GUI) and SynthEditCL (headless)
// link it. EditorLib doesn't use precompiled headers, so we don't include
// pch.h here — none of the code below needs WinRT/UWP types. The vcxproj
// entry for this file is removed; SynthEdit2 picks up the symbols through
// the EditorLib link.
#include "SynthEditAppBase.h"
#include "SynthEditDocBase.h" // for Document()->SelectedViewType (panel vs structure view)
#include "PropertiesBrowser.h"
#include "CUG.h"
#include "PatchParameter.h"
#include "Notify_msg.h"
#include "midi_defs.h" // ControllerType
#include "CLine2.h"
#include <cmath>
#include <format>
#include <cwctype>
#include <functional>
#include "experimental/builders.h"
#include "experimental/theme.h"
#include "BrowserFontSize.h"
#include <algorithm>
#include "CContainer.h"

using namespace gmpi;
using namespace gmpi::ui;
using namespace gmpi::drawing;

namespace
{
	// Shared layout metrics (used by both PropertiesBrowser::Body and createParameterFieldEditorView).
	// The two text metrics follow the Browser Font Size preference, so they are read
	// through functions rather than being constants: every use is inside Body(), which
	// re-runs when the preference changes. The scrollbar and the panel's outer margin
	// are chrome, not text, and stay put.
	inline float rowHeight()   { return SynthEdit::browserRowHeight(); }
	inline float lineSpacing() { return 3.0f * SynthEdit::browserFontScale(); }
	constexpr float scrollBarWidth{ 12 };
	constexpr float outerMargin{ 8 };

	// Clamp for the draggable label-column fraction, so neither column can collapse.
	constexpr float kMinColumnFraction{ 0.15f };
	constexpr float kMaxColumnFraction{ 0.85f };

	// Width (px) of the text area for a given panel bounds — mirrors the inset applied
	// in Body(). Used both to lay out columns and to convert a drag delta into a fraction.
	inline float propertiesTextAreaWidth(const gmpi::drawing::Rect& panelBounds)
	{
		return (panelBounds.right - panelBounds.left) - scrollBarWidth - 2.0f * outerMargin;
	}

	// Half-width (px) of the divider's grab zone near its line.
	constexpr float kDividerGrabHalfWidth{ 4.0f };

	// Invisible mouse target for the column divider. Emits only a RectangleMouseTarget (the
	// visible line is drawn separately in PropertiesBrowser::render). Added to the Form BEFORE
	// the ScrollPortal so it sits first in the mouse list and is hit-tested LAST — the scrolling
	// content gets first refusal, so the divider only claims presses in the empty column gap.
	struct ColumnDividerHitView : public gmpi::ui::builder::View
	{
		gmpi::drawing::Rect bounds;
		std::function<void(const gmpi::forms::primitive::PointerEvent*)> onGrab;
		std::function<void(bool)> onHoverChange;

		explicit ColumnDividerHitView(gmpi::drawing::Rect pbounds) : bounds(pbounds) {}

		gmpi::drawing::Rect getBounds() const override { return bounds; }
		void setBounds(gmpi::drawing::Rect b) override { bounds = b; }

		void Render(gmpi_forms::Environment* /*env*/, gmpi::forms::primitive::Canvas& canvas) const override
		{
			auto* hit = new gmpi::forms::primitive::RectangleMouseTarget(bounds);
			canvas.add(hit);
			// onPointerDown_callback also makes wantsClicks() true, which is required for the
			// hover scan to route setHover() here.
			hit->onPointerDown_callback = onGrab;
			hit->onHover_callback = onHoverChange;
		}
	};

	// Strip a leading '#' or '0x'/'0X' and validate a user-typed colour hex, returning a
	// canonical uppercase RRGGBB / AARRGGBB string, or "" if it isn't valid 6- or 8-digit hex.
	inline std::string normalizeHexColor(std::string s)
	{
		const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
		size_t b = 0, e = s.size();
		while (b < e && isSpace(s[b])) ++b;
		while (e > b && isSpace(s[e - 1])) --e;
		s = s.substr(b, e - b);

		if (!s.empty() && s.front() == '#') s.erase(0, 1);
		if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s.erase(0, 2);

		if (s.size() != 6 && s.size() != 8) return {};

		std::string out;
		for (char c : s)
		{
			if (c >= '0' && c <= '9') out += c;
			else if (c >= 'A' && c <= 'F') out += c;
			else if (c >= 'a' && c <= 'f') out += static_cast<char>(c - 'a' + 'A');
			else return {}; // not a hex digit
		}
		return out;
	}

	// Format a colour as a canonical uppercase hex string — RRGGBB when opaque, else AARRGGBB
	// (alpha in the high byte, matching colorFromHexString). Channels are linear-light, so
	// re-encode each to sRGB 8-bit (the inverse of colorFromHex's decode) for a lossless round-trip.
	inline std::string colorToHex(gmpi::drawing::Color c)
	{
		const int r = gmpi::drawing::linearPixelToSRGB(c.r);
		const int g = gmpi::drawing::linearPixelToSRGB(c.g);
		const int b = gmpi::drawing::linearPixelToSRGB(c.b);
		if (c.a >= 0.999f)
			return std::format("{:02X}{:02X}{:02X}", r, g, b);
		const int a = static_cast<int>(std::lround(c.a * 255.0f));
		return std::format("{:02X}{:02X}{:02X}{:02X}", a, r, g, b);
	}

	// A small filled square previewing a colour, parsed live from a hex-string State (the pin's
	// stored value). Observes the source, so it repaints when the hex is edited — no rebuild needed.
	// Clicking it opens the native colour picker; the chosen colour is written back via onPick.
	struct ColorSwatchView : public gmpi::ui::builder::View
	{
		gmpi::drawing::Rect bounds;
		mutable gmpi_forms::StateRef<std::wstring> value;
		std::function<void(const std::string& hex)> onPick;                 // set by Body: writes the pin
		mutable gmpi::shared_ptr<gmpi::api::IColorDialog> dialog;           // kept alive across the async call

		explicit ColorSwatchView(gmpi_forms::State<std::wstring>& source)
		{
			value.setSource(&source);
			value.addObserver([this]() { setDirty(); });
		}

		gmpi::drawing::Rect getBounds() const override { return bounds; }
		void setBounds(gmpi::drawing::Rect b) override { bounds = b; }

		void Render(gmpi_forms::Environment* env, gmpi::forms::primitive::Canvas& canvas) const override
		{
			namespace primitive = gmpi::forms::primitive;
			const auto color = gmpi::drawing::colorFromHexString(normalizeHexColor(WStringToUtf8(value.get())));

			auto* style = new primitive::ShapeStyle();
			style->fillColor = color;
			style->strokeColor = gmpi::ui::currentTheme().separator; // subtle border keeps light/empty colours visible
			canvas.add(style);

			auto r = getBounds(); // inset a touch so the square doesn't touch the cell edges
			r.left += 2.0f; r.top += 2.0f; r.right -= 2.0f; r.bottom -= 2.0f;
			canvas.add(new primitive::Rectangle(style, r));

			// Clicking the swatch opens the native colour picker.
			auto* hit = new primitive::RectangleMouseTarget(getBounds());
			canvas.add(hit);
			auto* self = const_cast<ColorSwatchView*>(this);
			hit->onPointerDown_callback = [self, env, color](const primitive::PointerEvent*)
			{
				if (!env || !env->dialogHost)
					return;

				gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
				env->dialogHost->createColorDialog(color, unknown.put());
				self->dialog = unknown.as<gmpi::api::IColorDialog>();
				if (!self->dialog)
					return; // no picker on this platform

				auto pick = self->onPick; // copy so the callback survives a rebuild
				self->dialog->showAsync(new gmpi::sdk::ColorDialogCallback(
					[pick](gmpi::drawing::Color chosen)
					{
						if (pick)
							pick(colorToHex(chosen));
					}));
			};
		}
	};
}

// combine a parameter handle and field type into one 64-bit key, for use in std::map lookup.
int64_t handleField(PatchParameter_base* p, ParameterFieldType field)
{
	return (static_cast<int64_t>(p->Handle()) << 32) | static_cast<int64_t>(field);
}

bool isNumeric(EPlugDataType dt)
{
	switch (dt)
	{
	case DT_BOOL:
	case DT_INT:
	case DT_INT64:
	case DT_FLOAT:
	case DT_DOUBLE:
	case DT_FSAMPLE:
		return true;
	default:
		return false;
	}
}

// Convert FT_AUTOMATION integer value to display string.
std::string AutomationToDisplayString(int32_t automation)
{
	if (automation == ControllerType::Learn)
		return "Learn..."; // armed, waiting for the next incoming controller

	if (automation < 0)
		return "<none>";

	const int controllerType = automation >> 24;
	const int controllerNumber = automation & 0xFFFF;

	struct ControllerInfo { const char* name; int id; bool showNumber; int numberMask; };
	static const ControllerInfo controllerInfo[] =
	{
		{ "<none>",					-1, false, 0 },
		{ "CC",						2,  true,  0x7F },
		{ "RPN",					3,  true,  0x3FFF },
		{ "NRPN",					4,  true,  0x3FFF },
		{ "SYSEX",					5,  false, 0 },
		{ "Poly Trigger",			24, true,  0x7F },
		{ "Poly Gate",				20, true,  0x7F },
		{ "Poly Pitch",				19, true,  0x7F },
		{ "Poly Velocity Key On",	21, true,  0x7F },
		{ "Poly Velocity Key Off",	22, true,  0x7F },
		{ "Poly Aftertouch",		23, true,  0x7F },
		{ "Voice ID",				27, false, 0 },
		{ "Bender",					6,  false, 0 },
		{ "Channel Pressure",		7,  false, 0 },
		{ "GlideStartPitch",		18, false, 0 },
	};

	for (const auto& info : controllerInfo)
	{
		if (info.id == controllerType)
		{
			if (info.showNumber)
				return std::format("{} {}", info.name, controllerNumber & info.numberMask);
			else
				return info.name;
		}
	}

	return "<unknown>";
}

void MidiAutomationToString::onParameterChanged(int32_t voice, const void* data, int32_t size)
{
	output.set(AutomationToDisplayString(RawToValue<int32_t>(data, size)));
}

// Format the raw value into the textbox's display string. Driven by 'data' (not a re-fetch),
// keyed on the datatype captured at bind time.
void ParamToText::onParameterChanged(int32_t voice, const void* data, int32_t size)
{
	switch (datatype)
	{
	case DT_INT:
		output.set(std::to_string(RawToValue<int32_t>(data, size)));
		break;

	case DT_INT64:
		output.set(std::to_string(RawToValue<int64_t>(data, size)));
		break;

	case DT_FLOAT:
		output.set(NiceDoubleToString(RawToValue<float>(data, size)));
		break;

	case DT_DOUBLE:
		output.set(NiceDoubleToString(RawToValue<double>(data, size)));
		break;

	case DT_TEXT:
		output.set(WStringToUtf8(RawToValue<std::wstring>(data, size)));
		break;

	case DT_STRING_UTF8:
		output.set(RawToValue<std::string>(data, size));
		break;

	case DT_BLOB:
	case DT_OBJECT:
	{
		std::string result;
		if (size > 256)
		{
			result = "<large blob>";
		}
		else
		{
			// convert to hex string.
			result.reserve(size * 3);
			const auto* bytes = static_cast<const uint8_t*>(data);
			for (int i = 0; i < size; ++i)
				result += std::format("{:02X} ", bytes[i]);

			if (!result.empty())
				result.pop_back(); // remove trailing space
		}

		output.set(result);
	}
	break;

	default:
		break;
	}
}

PropertiesBrowser::PropertiesBrowser(CSynthEditAppBase* p_app)
{
	app = p_app;
	columnFraction = std::clamp(app->settings.PropertiesLabelColumnFraction, kMinColumnFraction, kMaxColumnFraction);
}

PropertiesBrowser::~PropertiesBrowser()
{
	clear();

	app->UnRegisterObserver(&viewModel);
}

void PropertiesBrowser::Init()
{
	app->RegisterObserver(&viewModel);

	renderVisuals();
	viewModel.observers2.push_back(this);
}

PropertiesViewModel::~PropertiesViewModel()
{
	if (currentPatchManager)
		currentPatchManager->UnRegisterGui2(static_cast<gmpi::api::IParameterObserver*>(this));
}

void PropertiesViewModel::OnNotify(Notifier* sender, int lHint, void* pHint)
{
	switch (lHint)
	{
	case OM_SHOW_PROPERTIES:
	{
		auto newModule = reinterpret_cast<CDocOb*>(pHint);

		if(newModule == currentModule)
			return;

		currentModule = newModule;

		auto cug = dynamic_cast<CUG*>(currentModule);
		auto newPm = cug ? cug->get_patch_manager() : nullptr;
		if (newPm != currentPatchManager)
		{
			if (currentPatchManager)
				currentPatchManager->UnRegisterGui2(static_cast<gmpi::api::IParameterObserver*>(this));

			currentPatchManager = newPm;

			if (currentPatchManager)
				currentPatchManager->RegisterGui2(static_cast<gmpi::api::IParameterObserver*>(this));
		}

		// Watch the module's container so the layout States track panel moves/resizes.
		// Watched regardless of view type (costs nothing); the Layout section binds the
		// States only when the panel view is showing.
		{
			Notifier* newContainer = cug ? cug->Container() : nullptr; // main view has null container
			if (newContainer != layoutContainer)
			{
				if (layoutContainer)
					layoutContainer->UnRegisterObserver(this);

				layoutContainer = newContainer;

				if (layoutContainer)
					layoutContainer->RegisterObserver(this);
			}
			refreshLayoutStates();
		}

		invalidateView();
	}
	break;

	case OM_BROWSER_FONT_SIZE_CHANGED:
	{
		// Row heights and font sizes are chosen in Body(), so the new preference only
		// reaches the screen through a rebuild.
		invalidateView();
	}
	break;

	case OM_ONCHANGE_CHILD_POSITION_PANEL:
	{
		// current module moved/resized on the panel (dragged in the editor, or a committed
		// edit in a layout field). Only writes States — never invalidateView(), which would
		// synchronously destroy the very text editor whose commit fired this.
		if (pHint == (void*)currentModule)
			refreshLayoutStates();
	}
	break;

	case OM_NAME_CHANGE:
	{
		// a pin was renamed (this form's field, another view, or a tied IO pin propagating).
		// pHint doesn't identify the pin, so re-seed all of them. States only — see above.
		refreshPinNameStates();
	}
	break;

	case OM_ADD_CHILD:
	case OM_REMOVE_CHILD:
	{
		// A wire was added or removed in the watched container. If it touches the current
		// module, the pin rows' composition changes (a newly wired pin hides its value
		// editor, a disconnected one shows it again, autoduplicating modules grow/lose
		// pins) — that needs a rebuild; States can't add or remove rows. Safe here, unlike
		// mid-commit: wire changes originate on the structure view, never from inside one
		// of this form's own edit commits. The actual rebuild is deferred to the next
		// paint, by which time the connect/disconnect (including pin add/remove) is done.
		auto line = dynamic_cast<CLine2*>(reinterpret_cast<CDocOb*>(pHint));
		if (line && line->FromPlug && line->ToPlug)
		{
			auto mod = dynamic_cast<CUG*>(currentModule);
			if (mod && (line->FromPlug->UG() == mod || line->ToPlug->UG() == mod))
				invalidateView();
		}
	}
	break;

	case OM_DELETE:
	{
		// the watched container is being destroyed. Unregistering here is mandatory:
		// ~Notifier's NotifySafe(OM_DELETE) loops until each observer removes itself.
		if (sender == layoutContainer)
		{
			layoutContainer->UnRegisterObserver(this);
			layoutContainer = nullptr;
		}
	}
	break;
	}
}

void PropertiesViewModel::refreshLayoutStates()
{
	auto mod = dynamic_cast<CUG*>(currentModule);
	if (!mod)
		return;

	const auto r = mod->getViewObRect(CF_PANEL_VIEW);
	layoutX.set(std::to_string(r.left));
	layoutY.set(std::to_string(r.top));
	layoutW.set(std::to_string(r.right - r.left));
	layoutH.set(std::to_string(r.bottom - r.top));
}

void PropertiesViewModel::refreshPinNameStates()
{
	auto mod = dynamic_cast<CUG*>(currentModule);
	if (!mod)
		return;

	// iterate the module's live pin list (not the map) so a stale entry for a
	// since-deleted pin is never dereferenced.
	for (auto pin : mod->Plugs)
	{
		if (auto it = pinNameStates.find(pin); it != pinNameStates.end())
			it->second.set(WStringToUtf8(pin->getName()));
	}
}

void PropertiesBrowser::OnModelWillChange()
{
	clear();
	viewModel.more_converters.clear();
	viewModel.pinNameStates.clear(); // repopulated by Body(); widgets are already gone (clear() above)

	// When the displayed module changes, reset scroll to the top — otherwise a
	// smaller next-module's content can start scrolled off-screen. Skipped for
	// other refresh reasons (resize, theme change, parameter update) so the
	// user's scroll position is preserved.
	if (viewModel.currentModule != lastRenderedModule)
	{
		if (auto* scroll = dynamic_cast<gmpi_forms::State<float>*>(env.findState("ScrollPortal.scroll")))
			scroll->set(0.0f);
		lastRenderedModule = viewModel.currentModule;
	}

	redraw(); // just repaints.
} // TODO

// handle parameter update by dirtying relevant view.
gmpi::ReturnCode PropertiesViewModel::setParameter(int32_t handle, gmpi::Field pfield, int32_t voice, int32_t size, const uint8_t* data)
{
	const auto field = static_cast<ParameterFieldType>(pfield);

	// update the view model for any widget bound to this parameter/field.
	const int64_t handle_field = (static_cast<int64_t>(handle) << 32) | static_cast<int64_t>(field);

	if (auto it = more_converters.find(handle_field); it != more_converters.end())
	{
		auto& converter = *(it->second);
		converter.onParameterChanged(voice, data, size);
	}

	return gmpi::ReturnCode::Ok;
}

// Sets up the one-way (model -> UI) binding for a parameter field's text State, and
// returns the matching 'validateAndSave' back-channel (UI -> model) for the caller to
// install on the textbox. The State is only ever written model -> UI (via setParameter /
// UpdateGui); the textbox routes user edits through the returned lambda instead of writing
// the State directly. That avoids the two-way notification churn that previously left an
// invalid entry (e.g. "A" typed into a numeric field) stuck on screen.
std::function<void(const std::string&)> bindParameterToTextBox(
	PatchParameter_base* p,
	ParameterFieldType fieldType,
	gmpi_forms::StateRef<std::string>& stateref,
	std::map<int64_t, std::unique_ptr<ParamObserver>>& more_converters
)
{
	int datatype{};
	p->GetDatatype(fieldType, &datatype);

	// model -> UI: a ParamToText formats the parameter value into the textbox's State.
	// (it binds 'stateref' to its output in its constructor.)
	more_converters[handleField(p, fieldType)] = std::make_unique<ParamToText>(stateref, p, fieldType, datatype);

	// sync intial value from model.
	p->UpdateGui(fieldType);

	// build the UI -> model back-channel (the textbox's 'validateAndSave'), keyed on datatype.
	switch (datatype)
	{
	case DT_INT:
		return [p, fieldType](const std::string& val)
			{
				p->SetValue(RawView(static_cast<int32_t>(atoi(val.c_str()))), fieldType);
				// SetValue only notifies on a real change, so typed input that normalizes to
				// the unchanged value (e.g. "007", "abc" -> 0) would stay on screen. Re-seed
				// the display from the model regardless.
				p->UpdateGui(fieldType);
			};

	case DT_INT64:
		return [p, fieldType](const std::string& val)
			{
				p->SetValue(RawView(static_cast<int64_t>(atoll(val.c_str()))), fieldType);
				p->UpdateGui(fieldType); // re-format display even when the value didn't change
			};

	case DT_FLOAT:
		return [p, fieldType](const std::string& val)
			{
				// avoid updating value by using the simpler display text unless the user made a significant change.
				const auto dispText = NiceDoubleToString((float)p->GetValue(fieldType));
				const auto newVal = atof(val.c_str());
				const auto newText = NiceDoubleToString(newVal);

				/* 99% correct, except when patch value is 0.999 => "1.000" => 1.0 => "1.00" (two zeros)
				if (dispText == newText)
					return; // no change.
				*/

				if(atof(dispText.c_str()) == atof(newText.c_str()))
				{
					// no change to the model, so no notification will fire — refresh the display
					// with the clean formatted value ourselves (e.g. user-entered "7.000" -> "7.00").
					p->UpdateGui(fieldType);
					return;
				}

				p->SetValue(RawView(static_cast<float>(newVal)), fieldType);
			};

	case DT_DOUBLE:
		return [p, fieldType](const std::string& val)
			{
				// avoid updating value by using the simpler display text unless the user made a significant change.
				const auto dispText = NiceDoubleToString((double)p->GetValue(fieldType));
				const auto newVal = atof(val.c_str());
				const auto newText = NiceDoubleToString(newVal);

				//if (dispText == newText)
				//	return; // no change.
				if(atof(dispText.c_str()) == atof(newText.c_str()))
				{
					p->UpdateGui(fieldType); // no notification will fire — re-format the display ourselves
					return;
				}

				p->SetValue(RawView(newVal), fieldType);
			};

	case DT_TEXT:
		return [p, fieldType](const std::string& val)
			{
				p->SetValue(RawView(Utf8ToWstring(val)), fieldType);
			};

	case DT_STRING_UTF8:
		return [p, fieldType](const std::string& val)
			{
				p->SetValue(RawView(val), fieldType);
			};

	case DT_BLOB:
	case DT_OBJECT:
		return [p, fieldType](const std::string& val)
			{
				const auto& hex = val;

				// hex string back to bytes
				std::vector<uint8_t> bytes;
				bytes.reserve(hex.size() / 2);

				for (size_t i = 0; i < hex.size(); )
				{
					// Skip spaces
					while (i < hex.size() && hex[i] == ' ')
						++i;

					if (i + 1 >= hex.size())
						break;

					// Parse two hex characters
					auto hexCharToInt = [](char c) -> int {
						if (c >= '0' && c <= '9') return c - '0';
						if (c >= 'A' && c <= 'F') return c - 'A' + 10;
						if (c >= 'a' && c <= 'f') return c - 'a' + 10;
						return -1;
						};

					int hi = hexCharToInt(hex[i]);
					int lo = hexCharToInt(hex[i + 1]);

					// invalid character cause us to quit (rather than write incomplete or garbage into value)
					if (hi < 0 || lo < 0)
					{
						p->UpdateGui(fieldType); // restore the display from the (unchanged) stored value
						return;
					}

					bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));

					i += 2;
				}
				p->SetValue(RawView(bytes), fieldType);
				p->UpdateGui(fieldType); // re-format display (canonical hex) even when the bytes didn't change
			};

	default:
		assert(false); // TODO
		break;
	}

	return {};
}

gmpi::ui::builder::View* PropertiesBrowser::createParameterFieldEditorView(
	  PatchParameter_base* p
	, ParameterFieldType fieldType
	, gmpi::drawing::Rect control_bounds
)
{
	int datatype{};
	p->GetDatatype(fieldType, &datatype);

	bool isFilename{};

	// Can identify ENUM and filename parameters by checking what type of metadata they have.
	if (FT_VALUE == fieldType)
	{
		int metadatatype{};

		if (DT_INT == datatype)
		{
			p->GetDatatype(FT_ENUM_LIST, &metadatatype);

			if (metadatatype == DT_TEXT)
				datatype = DT_ENUM;
		}
		if (DT_TEXT == datatype)
		{
			p->GetDatatype(FT_FILE_EXTENSION, &metadatatype);
			isFilename = metadatatype == DT_TEXT;
		}
	}

	switch (datatype)
	{
	case DT_ENUM:
	{
		auto combo = std::make_unique<gmpi::ui::builder::ComboBoxView>();
		combo->bounds = control_bounds;
		auto* comboPtr = combo.get();

		// bind the enum list -> combo (one-way; the list isn't user-editable, so the
		// returned validateAndSave back-channel is intentionally discarded).
		bindParameterToTextBox(p, FT_ENUM_LIST, combo->enum_list, viewModel.more_converters);

		// one-way (model -> UI) binding of the parameter value to the combo; the selection
		// is saved via validateAndSave.
		viewModel.more_converters[handleField(p, fieldType)] = std::make_unique<ParamToState<int32_t>>(combo->enum_value, p, fieldType);
		combo->validateAndSave = [p, fieldType](int32_t selectedId)
			{
				p->SetValue(RawView(selectedId), fieldType);
			};

		gmpi::ui::builder::ThreadLocalCurrentBuilder->push_back(std::move(combo));
		return comboPtr;
	}
	break;

	case DT_BOOL:
	{
		auto tickBox = std::make_unique<gmpi::ui::builder::TickBox>();
		auto* tickBoxPtr = tickBox.get();

		// one-way (model -> UI) binding to tickbox; the click is saved via validateAndSave.
		viewModel.more_converters[handleField(p, fieldType)] = std::make_unique<ParamToState<bool>>(tickBox->value, p, fieldType);
		p->UpdateGui(fieldType); // set initial value from model.

		tickBox->validateAndSave = [p, fieldType](bool newValue)
			{
				p->SetValue(RawView(newValue), fieldType);
			};

		tickBox->bounds = control_bounds;

		// Left-align the square tickbox at the value column's left edge: a nested grid holds
		// the square cell plus an empty filler that absorbs the rest of the column. (The grid
		// maps one column track per child, so the filler is a real, if invisible, child.)
		gmpi::ui::Grid boolCell(
			  { .gap = lineSpacing(), .auto_rows = rowHeight(), .auto_flow = builder::ViewParent::eAutoFlow::columns, .column_widths = { rowHeight(), builder::fr(1.0f) } }
			, {}
		);
		gmpi::ui::builder::ThreadLocalCurrentBuilder->push_back(std::move(tickBox));
		gmpi::ui::Label filler("");

		return tickBoxPtr;
	}
	break;

	case DT_TEXT:
	case DT_STRING_UTF8:
	case DT_FLOAT:
	case DT_DOUBLE:
	case DT_INT:
	case DT_INT64:
	case DT_BLOB:
	case DT_OBJECT:
	{
		auto textbox = std::make_unique<gmpi::ui::builder::TextEditView>();
		textbox->bounds = control_bounds;
		textbox->rightAlign = false; // all value fields are left-aligned
		textbox->multiLine = (DT_BLOB == datatype || DT_OBJECT == datatype) || ((DT_TEXT == datatype || DT_STRING_UTF8 == datatype) && !isFilename);

		auto* textboxPtr = textbox.get();

		// one-way (model -> UI) binding; user edits are routed back through validateAndSave.
		textbox->validateAndSave = bindParameterToTextBox(p, fieldType, textbox->text, viewModel.more_converters);
		gmpi::ui::builder::ThreadLocalCurrentBuilder->push_back(std::move(textbox));
		return textboxPtr;
	}
	break;
	};

	return {};
}

// create the builders (that create the drawable primatives)
void PropertiesBrowser::Body()
{
	auto* mod = dynamic_cast<CUG*>(viewModel.currentModule);
	if (!mod)
		return;

	viewModel.more_converters.clear();

	// Column-divider hit target — added to the Form (as its first child, before the
	// ScrollPortal) so it is hit-tested LAST: the scrolling content gets first refusal, and
	// the divider only grabs presses that land in the empty column gap. It draws nothing (the
	// visible line is painted in render()); it just provides the drag + hover behaviour.
	{
		const float dividerX = dividerLineX();
		auto hit = std::make_unique<ColumnDividerHitView>(
			gmpi::drawing::Rect{ dividerX - kDividerGrabHalfWidth, bounds.top, dividerX + kDividerGrabHalfWidth, bounds.bottom });

		hit->onHoverChange = [this](bool over)
		{
			if (over != dividerHovered) { dividerHovered = over; redraw(); } // grey<->white line
		};
		hit->onGrab = [this](const gmpi::forms::primitive::PointerEvent* e)
		{
			draggingDivider = true;
			dividerDragStartFraction = columnFraction;
			e->boss->captureMouse([this](gmpi::drawing::Size delta)
			{
				const float w = propertiesTextAreaWidth(bounds);
				if (w > 1.0f)
				{
					columnFraction = std::clamp(columnFraction + delta.width / w, kMinColumnFraction, kMaxColumnFraction);
					viewModel.invalidateView(); // rebuild Body() with the new fraction (live feedback)
				}
			});
		};
		this->push_back(std::move(hit));
	}

	// must be before accessing ThreadLocalCurrentBuilder, since
	// it substitutes itself
	gmpi::ui::ScrollPortal _(bounds);

	using gmpi::ui::builder::fr;
	using gmpi::ui::builder::auto_size;
	using eAutoFlow = gmpi::ui::builder::ViewParent::eAutoFlow;

	// Section headings ("LAYOUT", "PINS", ...) track the preference alongside the rows.
	const float headingHeight = 14.f * SynthEdit::browserFontScale();

	gmpi::drawing::Rect textArea = bounds;
	textArea.right -= scrollBarWidth;
	textArea.left += outerMargin;
	textArea.right -= outerMargin;
	textArea.top += outerMargin;
	textArea.bottom -= outerMargin;

	// Fixed-pixel width of the label column, shared by every row grid so their value
	// editors line up and a single vertical divider marks the boundary for all of them.
	// (A fixed px — rather than an fr fraction — keeps the boundary identical across rows
	// that have different trailing columns, e.g. the file-browse button.)
	const float labelColumnWidth = std::clamp(columnFraction, kMinColumnFraction, kMaxColumnFraction) * (textArea.right - textArea.left);

	// outer vertical Grid: each row's height comes from its child via auto_size.
	gmpi::ui::Grid outer(
		  { .gap = lineSpacing(), .auto_flow = eAutoFlow::rows, .default_track_size = auto_size() }
		, textArea
	);

	// Module type heading
	{
		auto moduleTypeName = std::wstring(GetName(mod->getType()));
		for (auto& ch : moduleTypeName)
			ch = static_cast<wchar_t>(std::towupper(ch));

		gmpi::ui::Label heading(WStringToUtf8(moduleTypeName), { 0, 0, 0, headingHeight });
	}

	// Module name field
	{
		// direct State-is-model: writing 'name' updates the model and fires its observer
		// (modified flag, presenter refresh). Routed through validateAndSave since StateRef is read-only.
		gmpi::ui::TextEdit field(
			mod->name
			, [mod](const std::string& v) { mod->name.set(v); }
		);
		field.view->bounds = { 0, 0, 0, rowHeight() }; // height is what the outer Grid reads via auto_size
	}

	// Layout section (panel view only): Figma-style X/Y/W/H editors for the object's panel
	// layout rectangle. Shown only when the PANEL view is active and the module actually has a
	// panel placement (non-panel modules return an empty rect from getViewObRect).
	if (app && app->Document() && app->Document()->SelectedViewType == CF_PANEL_VIEW)
	{
		const gmpi::drawing::RectL panelRect = mod->getViewObRect(CF_PANEL_VIEW);
		const bool hasPanelRect = (panelRect.right > panelRect.left) && (panelRect.bottom > panelRect.top);
		if (hasPanelRect)
		{
			gmpi::ui::Spacer spacer({ 0, 0, 0, headingHeight });
			gmpi::ui::Label heading("LAYOUT", { 0, 0, 0, headingHeight });

			// One labelled, editable integer row bound to a component of the panel rect.
			// 'state' is the view-model's display State for the field — seeded and kept fresh
			// by the view-model's container watch (OM_ONCHANGE_CHILD_POSITION_PANEL), so
			// dragging the module on the panel live-updates the field. 'extract' reads the
			// field's value from a rect; 'mutate' writes a typed value back.
			auto layoutRow = [&](
				  const char* fieldLabel
				, gmpi_forms::State<std::string>& state
				, std::function<int(const gmpi::drawing::RectL&)> extract
				, std::function<void(gmpi::drawing::RectL&, int)> mutate)
			{
				gmpi::ui::Grid row(
					  { .gap = lineSpacing(), .auto_rows = rowHeight(), .auto_flow = eAutoFlow::columns, .column_widths = { labelColumnWidth, fr(1.0f) } }
					, {}
				);
				gmpi::ui::Label label(fieldLabel);

				gmpi::ui::TextEdit field(
					state
					, [mod, extract, mutate, statePtr = &state](const std::string& v)
					{
						auto r = mod->getViewObRect(CF_PANEL_VIEW);
						mutate(r, atoi(v.c_str()));
						// setViewObRect no-ops if unchanged, else sets the modified flag, repaints
						// the panel, and notifies the view-model, which re-seeds all four layout
						// States. NOTE: don't invalidateView() here — that would synchronously
						// destroy this text editor mid-commit.
						mod->setViewObRect(CF_PANEL_VIEW, r);
						// No notification fires when the stored rect didn't change (e.g. input that
						// normalizes back to the current value), so correct the display regardless.
						statePtr->set(std::to_string(extract(mod->getViewObRect(CF_PANEL_VIEW))));
					}
				);
				//field.view->rightAlign = false;
				//field.view->multiLine = false;
			};

			// X/Y move the object (preserving size); W/H resize it (min 1px).
			layoutRow("X", viewModel.layoutX,
				[](const gmpi::drawing::RectL& r) { return r.left; },
				[](gmpi::drawing::RectL& r, int nx) { const int w = r.right - r.left; r.left = nx; r.right = nx + w; });
			layoutRow("Y", viewModel.layoutY,
				[](const gmpi::drawing::RectL& r) { return r.top; },
				[](gmpi::drawing::RectL& r, int ny) { const int h = r.bottom - r.top; r.top = ny; r.bottom = ny + h; });
			layoutRow("W", viewModel.layoutW,
				[](const gmpi::drawing::RectL& r) { return r.right - r.left; },
				[](gmpi::drawing::RectL& r, int nw) { r.right = r.left + std::max(1, nw); });
			layoutRow("H", viewModel.layoutH,
				[](const gmpi::drawing::RectL& r) { return r.bottom - r.top; },
				[](gmpi::drawing::RectL& r, int nh) { r.bottom = r.top + std::max(1, nh); });
		}
	}

	// Pin rows
	const bool anyPinsVisible = std::any_of(mod->Plugs.begin(), mod->Plugs.end(),
		[](auto pin) { return !(pin->DisableIfNotConnected() || (!pin->can_rename() && !pin->can_set_value())); });

	if (anyPinsVisible)
	{
		gmpi::ui::Spacer spacer({ 0, 0, 0, headingHeight });
		gmpi::ui::Label heading("PINS", { 0, 0, 0, headingHeight });

		for (auto pin : mod->Plugs)
		{
			if (pin->DisableIfNotConnected() || (!pin->can_rename() && !pin->can_set_value()))
				continue;

			constexpr float buttonWidth{ 20 };
			const auto isFilename = (pin->getDatatype() == DT_TEXT || pin->getDatatype() == DT_STRING_UTF8) && pin->is_filename();
			const auto isColor = pin->getDatatype() == DT_STRUCT && pin->getClassName() == "color";

			// General rule: don't edit a connected pin's default, its value is driven by its wire.
			// Exception: an editable (settable) output pin sets its own value regardless of connections.
			const bool valueDrivenByWire = pin->HasActiveConnections() && !pin->isSettableOutput();

			std::vector<float> pinColumns = [&]() -> std::vector<float>
			{
				if (!pin->can_set_value())
					return { fr(1.0f) };
				// A connected pin's value is driven by its wire, so it shows no value editor —
				// just the name spans the row (see the value-cell guard below).
				if (valueDrivenByWire)
					return { fr(1.0f) };
				if (pin->getDatatype() == DT_BOOL)
					return { labelColumnWidth, fr(1.0f) }; // tickbox is left-aligned within the value column via a nested grid
				if (isNumeric(pin->getDatatype()))
					return { labelColumnWidth, fr(1.0f) };
				if (isFilename)
					return { labelColumnWidth, fr(1.0f), buttonWidth };
				if (isColor)
					return { labelColumnWidth, fr(1.0f), rowHeight() }; // hex textbox + square swatch preview
				return { labelColumnWidth, fr(1.0f) };
			}();

			gmpi::ui::Grid grid(
				  { .gap = lineSpacing(), .auto_rows = rowHeight(), .auto_flow = eAutoFlow::columns, .column_widths = std::move(pinColumns) }
				, {}
			);

			// name cell
			if (pin->can_rename())
			{
				// one-way (model -> UI) binding to the view-model's per-pin name State — kept
				// fresh via OM_NAME_CHANGE, so renames from elsewhere (another view, a tied IO
				// pin) show up live. The rename is routed back through validateAndSave.
				auto& state = viewModel.pinNameStates[pin];
				state.set(WStringToUtf8(pin->getName()));

				gmpi::ui::TextEdit textbox(
					state
					// changes from UI to model.
					, [pin](const std::string& val) -> void
						{
							pin->setName(Utf8ToWstring(val));
						}
				);
			}
			else
			{
				gmpi::ui::Label label(WStringToUtf8(pin->getName()));
			}

			// value cell — only for pins that set their own value: an unconnected settable input,
			// or an editable (settable) output. A connected pin whose value is overridden by its
			// wire shows just its name, as its (read-only) default would be misleading. Non-settable
			// pins likewise emit no value widget.
			if (pin->can_set_value() && !valueDrivenByWire)
			{
				const auto readonly = false; // value cell only shown for pins that set their own value (unconnected input, or editable output)

				if (pin->getDatatype() == DT_ENUM)
				{
					gmpi::ui::ComboBox combo_builder({});
					auto& combo = combo_builder.view;

					// bind the enum list -> combo
					{
						auto state = std::make_unique< gmpi_forms::State<std::string> >(WStringToUtf8(pin->getDefaultEnumList()));

						combo->enum_list.setSource(state.get());
						combo->selfOwnedStates.push_back(std::move(state));
					}

					// bind the enum value one-way (model -> UI) via a converter on m_default, so picking
					// an item refreshes the combo through SetDefault; the selection is saved via the back-channel.
					{
						auto converter = std::make_unique<StateTypeConverter<std::wstring, int32_t> >();
						converter->from.setSource(&pin->m_default);
						converter->convertForward = [](std::wstring val) -> int32_t
							{
								return (int32_t)StringToInt(val);
							};
						converter->init();

						combo->enum_value.setSource(&converter->to);
						combo->validateAndSave = [pin](int32_t selectedId)
							{
								pin->SetDefault(std::to_wstring(selectedId));
							};
						combo->selfOwnedStates.push_back(std::move(converter));
						combo->readOnly = readonly;
					}
				}
				else if (pin->getDatatype() == DT_BOOL)
				{
					// one-way (model -> UI) binding; the click is validated and saved via the back-channel.
					auto converter = std::make_unique<StateTypeConverter<std::wstring, bool> >();
					{
						converter->from.setSource(&pin->m_default);
						converter->convertForward = [pin](std::wstring val) -> bool
							{
								return (bool)(1 == StringToInt(val));
							};
						converter->init();
					}

					auto tickBox = std::make_unique<gmpi::ui::builder::TickBox>(converter->to);
					tickBox->validateAndSave = [pin](bool newValue) -> void
						{
							pin->SetDefault(std::to_wstring(newValue));
						};
					tickBox->selfOwnedStates.push_back(std::move(converter));
					tickBox->readOnly = readonly;

					// Left-align the square tickbox at the value column's left edge: a nested grid holds
					// the square cell plus an empty filler that absorbs the rest of the column. (The grid
					// maps one column track per child, so the filler is a real, if invisible, child.)
					gmpi::ui::Grid boolCell(
						  { .gap = lineSpacing(), .auto_rows = rowHeight(), .auto_flow = eAutoFlow::columns, .column_widths = { rowHeight(), fr(1.0f) } }
						, {}
					);
					gmpi::ui::builder::ThreadLocalCurrentBuilder->push_back(std::move(tickBox));
					gmpi::ui::Label filler("");
				}
				else if (pin->getDatatype() == DT_INT || pin->getDatatype() == DT_INT64)
				{
					// nicely formated integer number.
					// one-way (model -> UI) binding; the edit is validated and saved via the back-channel.
					auto converter = std::make_unique<StateTypeConverter<std::wstring, std::string> >();
					{
						converter->from.setSource(&pin->m_default);
						converter->convertForward = [pin](std::wstring val) -> std::string
							{
								return std::to_string(atoi(WStringToUtf8(val).c_str()));
							};
						converter->init();
					}

					gmpi::ui::TextEdit textbox(
						converter->to

					// we don't want any two-way bindings. we want a monodirectional round-trip.
					// this lambda validates the entered value, and only then updates the model, avoiding notification and callback churn.
					, [pin](const std::string& val) -> void
						{
							const auto currentDefault = pin->GetDefault();

							// only update the default if the user made a meaningful change. e.g. "0" -> "0.0" = NO change.
							const auto prevText = std::to_string(atoi(WStringToUtf8(currentDefault).c_str()));
							const auto newText  = std::to_string(atoi(val.c_str()));

							if (prevText != newText)
								pin->SetDefault(Utf8ToWstring(newText));
						}
					);
					textbox.view->selfOwnedStates.push_back(std::move(converter));
					textbox.view->readOnly = readonly;
				}
				else if (pin->getDatatype() == DT_FLOAT || pin->getDatatype() == DT_DOUBLE || pin->getDatatype() == DT_FSAMPLE)
				{
					// one-way (model -> UI) binding; the edit is validated and saved via the back-channel.
					auto converter = std::make_unique<StateTypeConverter<std::wstring, std::string> >();
					{
						converter->from.setSource(&pin->m_default);
						converter->convertForward = [pin](std::wstring val) -> std::string
							{
								return NiceDoubleToString(atof(WStringToUtf8(val).c_str()));
							};

						converter->init();
					}

					gmpi::ui::TextEdit textbox(
						converter->to

					// we don't want any two-way bindings. we want a monodirectional round-trip.
					// this lambda validates the entered value, and only then updates the model, avoiding notification and callback churn.
					// otherwise entering say "A" in a float value text-entry that is already 0.0 leaves "A" showing since "A' translates to 0.0 and that "not a change"
					, [pin](const std::string& val) -> void
						{
							const auto currentDefault = pin->GetDefault();

							// only update the default if the user made a meaningful change. e.g. "0" -> "0.0" = NO change.
							const auto prevText = NiceDoubleToString(atof(WStringToUtf8(currentDefault).c_str()));
							const auto newText = NiceDoubleToString(atof(val.c_str()));

							if(atof(prevText.c_str()) != atof(newText.c_str()))
								pin->SetDefault(Utf8ToWstring(stripRedundantTrailingZeros(newText))); // change.
						}
					);

					textbox.view->selfOwnedStates.push_back(std::move(converter));
					textbox.view->readOnly = readonly;
				}
				else if (isColor)
				{
					// struct:color pin — edited as a hex string, with a live swatch preview.
					// one-way (model -> UI): show the stored hex; the edit is validated (must be
					// 6/8-digit hex) and saved via the back-channel.
					auto converter = std::make_unique<StateTypeConverter<std::wstring, std::string> >();
					converter->from.setSource(&pin->m_default);
					converter->convertForward = [](std::wstring val) -> std::string
						{
							return WStringToUtf8(val);
						};
					converter->init();

					gmpi::ui::TextEdit textbox(
						converter->to
						, [pin](const std::string& val) -> void
							{
								const auto hex = normalizeHexColor(val);
								if (hex.empty())
									return; // not valid hex — leave the stored value unchanged

								const auto newValue = Utf8ToWstring(hex);
								if (newValue != pin->GetDefault())
									pin->SetDefault(newValue);
							}
					);
					textbox.view->selfOwnedStates.push_back(std::move(converter));
					textbox.view->rightAlign = false;
					textbox.view->multiLine = false;
					textbox.view->readOnly = readonly;

					// Square swatch preview (third column). Observes the pin value directly, so it
					// repaints live as the hex is edited. Clicking it opens a colour picker.
					auto swatch = std::make_unique<ColorSwatchView>(pin->m_default);
					swatch->onPick = [pin](const std::string& hex)
					{
						pin->SetDefault(Utf8ToWstring(hex));
					};
					gmpi::ui::builder::ThreadLocalCurrentBuilder->push_back(std::move(swatch));
				}
				else if(pin->getDatatype() != DT_STRUCT) // DT_TEXT and friends
				{
					assert(pin->getDatatype() == DT_TEXT || pin->getDatatype() == DT_STRING_UTF8 || pin->getDatatype() == DT_BLOB || pin->getDatatype() == DT_OBJECT);

					auto converter = std::make_unique<StateTypeConverter<std::wstring, std::string> >();
					converter->from.setSource(&pin->m_default);
					converter->convertForward = [pin](std::wstring val) -> std::string
						{
							return WStringToUtf8(val);
						};
					converter->init();

					gmpi::ui::TextEdit textbox(
						converter->to
						// one-way (model -> UI) binding; the edit is saved via the back-channel.
						, [pin](const std::string& val) -> void
							{
								pin->SetDefault(Utf8ToWstring(val));
							}
					);
					textbox.view->selfOwnedStates.push_back(std::move(converter));
					textbox.view->rightAlign = false; // all value fields are left-aligned
					textbox.view->multiLine = !isFilename;
					textbox.view->readOnly = readonly;

					if (isFilename && !readonly)
					{
						// second converter (one-way, model -> UI) resolves the short name to a full path
						// for the browse dialog; the chosen path is saved via the back-channel.
						auto converter2 = std::make_unique<StateTypeConverter<std::wstring, std::string> >();
						converter2->from.setSource(&pin->m_default);
						converter2->convertForward = [pin](std::wstring val) -> std::string
							{
								const auto full_path = pin->UG()->Application()->ResolveFilename(val, pin->getFileExt());
								return WStringToUtf8(full_path);
							};
						converter2->init();

						auto browseButton = std::make_unique<gmpi::ui::builder::FileBrowseButtonView>(gmpi::drawing::Rect{});

						// A pin the module writes (e.g. Wave Recorder's output file) browses with a Save
						// dialog, so a not-yet-existing name can be typed. One it only reads gets an Open
						// dialog, which neither insists on a new name nor warns about overwriting.
						browseButton->saveMode = pin->is_filename_writable();

						browseButton->value.setSource(&converter2->to);
						browseButton->validateAndSave = [pin](const std::string& fullPath) -> void
							{
								const auto currentDefault = pin->GetDefault();
								const auto current_full_path = WStringToUtf8(pin->UG()->Application()->ResolveFilename(currentDefault, pin->getFileExt()));

								if(fullPath == current_full_path) // paths are logically equivalent, even if textually different. e.g. "arrow_small" == "arrow_small.bmp"
									return; // no change.

								const auto shortpath = pin->UG()->Application()->ShortenFilename(fullPath, WStringToUtf8(pin->getFileExt()));
								pin->SetDefault(Utf8ToWstring(shortpath));
							};
						browseButton->selfOwnedStates.push_back(std::move(converter2));

						gmpi::ui::builder::ThreadLocalCurrentBuilder->push_back(std::move(browseButton));
					}
				}
			}
		}
	}

	// Parameters: gather first, then emit if non-empty.
	std::multimap<std::wstring, PatchParameter_base*> sortedParameters;
	{
		IGuiHostParameterIterator* it{};
		mod->get_patch_manager()->GetParameterIterator(mod->Handle(), &it);
		bool is_done{};
		it->First();
		it->IsDone(&is_done);
		while (!is_done)
		{
			auto p = it->Current();
			assert(p);

			/* TODO
			bool isHostControlled = GetFieldBool(FT_HOST_PARAMETER);
			bool isPrivate = GetFieldBool(FT_PRIVATE);
			if(isHostControlled && isPrivate) // ignore private host controls, but not public ones.
				continue;
			*/

			const auto path = (std::wstring)p->GetValue();
			sortedParameters.insert({ path, p });

			it->Next();
			it->IsDone(&is_done);
		}
		it->Release();
	}

	if (!sortedParameters.empty())
	{
		gmpi::ui::Spacer spacer({ 0, 0, 0, headingHeight });
		gmpi::ui::Label heading("PARAMETERS", { 0, 0, 0, headingHeight });

		for (auto& it2 : sortedParameters)
		{
			auto p = it2.second;

			// Each parameter is a vertical sub-Grid wrapping all of its rows.
			gmpi::ui::Grid paramGroup(
				  { .gap = lineSpacing(), .auto_flow = eAutoFlow::rows, .default_track_size = auto_size() }
				, {}
			);

			// Name row (full-width single cell)
			// TODO !!! Placeholder Text - faint, italicized text inside the field (e.g., "e.g., john@example.com") that disappears upon clicking or typing.
			if (p->can_rename())
			{
				createParameterFieldEditorView(p, ParameterFieldType::FT_SHORT_NAME, { 0, 0, 0, rowHeight() });
			}
			else
			{
				gmpi::ui::Label label(WStringToUtf8(p->GetName()), { 0, 0, 0, rowHeight() });
			}

			// Hint row
			{
				gmpi::ui::Grid hintRow(
					  { .gap = lineSpacing(), .auto_rows = rowHeight(), .auto_flow = eAutoFlow::columns, .column_widths = { labelColumnWidth, fr(1.0f) } }
					, {}
				);
				gmpi::ui::Label label("Hint");
				createParameterFieldEditorView(p, ParameterFieldType::FT_HINT, {});
			}

			// Datatype detection (TODO: Text Entries connected to text-to-float show as having filename???)
			bool isFilename{};
			bool isEnum{};
			int datatype{};
			{
				p->GetDatatype(FT_VALUE, &datatype);
				if (DT_TEXT == datatype)
				{
					const auto extension = (std::wstring)p->GetValue(FT_FILE_EXTENSION);
					isFilename = !extension.empty();
				}
				else if (DT_INT == datatype)
				{
					// ENUM parameters are DT_INT with FT_ENUM_LIST metadata.
					int metadatatype{};
					p->GetDatatype(FT_ENUM_LIST, &metadatatype);
					isEnum = metadatatype == DT_TEXT;
				}
			}

			// Value row
			{
				constexpr float buttonWidth{ 20 };

				// column layout depends on the editor's datatype
				std::vector<float> valueColumns = [&]() -> std::vector<float>
				{
					if (isEnum)
						return { labelColumnWidth, fr(1.0f) };
					if (datatype == DT_BOOL)
						return { labelColumnWidth, fr(1.0f) }; // tickbox is left-aligned within the value column via a nested grid
					if (isNumeric((EPlugDataType)datatype))
						return { labelColumnWidth, fr(1.0f) };
					if (isFilename)
						return { labelColumnWidth, fr(1.0f), buttonWidth };
					return { labelColumnWidth, fr(1.0f) };
				}();

				gmpi::ui::Grid valueRow(
					  { .gap = lineSpacing(), .auto_rows = rowHeight(), .auto_flow = eAutoFlow::columns, .column_widths = std::move(valueColumns) }
					, {}
				);

				gmpi::ui::Label label("Value");

				auto* view = createParameterFieldEditorView(p, ParameterFieldType::FT_VALUE, {});

				if (view && isFilename)
				{
					const auto filename  = (std::wstring) p->GetValue(ParameterFieldType::FT_VALUE);
					const auto extension = (std::wstring) p->GetValue(ParameterFieldType::FT_FILE_EXTENSION);
					const auto extension8 = WStringToUtf8(extension);

					std::string full_filename = WStringToUtf8(
						app->ResolveFilename(
							filename,
							extension
						));

					// one-way (model -> UI) full-path State for the browse dialog; the chosen path
					// is shortened and saved via the back-channel.
					auto state2 = std::make_unique< gmpi_forms::State<std::string> >(full_filename);

					auto browseButton = std::make_unique<gmpi::ui::builder::FileBrowseButtonView>(gmpi::drawing::Rect{});

					// see the pin browse button above: Save dialog for a file the module writes,
					// Open dialog for one it only reads.
					browseButton->saveMode = p->isFilenameWritable();

					browseButton->value.setSource(state2.get());
					browseButton->validateAndSave = [p, extension8](const std::string& fullPath) -> void
						{
							auto app = p->getPatchManager()->Application();

							const auto shortpath = app->ShortenFilename(fullPath, extension8);
							auto shortpathw = Utf8ToWstring(shortpath);
							p->SetValue(RawView(shortpathw), ParameterFieldType::FT_VALUE);
						};
					browseButton->selfOwnedStates.push_back(std::move(state2));

					gmpi::ui::builder::ThreadLocalCurrentBuilder->push_back(std::move(browseButton));
				}
			}

			// ENUM-as-DT_INT remap, used by Low/High and MIDI gating below.
			if (DT_INT == datatype)
			{
				int metadatatype{};
				p->GetDatatype(FT_ENUM_LIST, &metadatatype);
				if (metadatatype == DT_TEXT)
					datatype = DT_ENUM;
			}

			// Low/High rows (numeric only)
			if (DT_FLOAT == datatype || DT_DOUBLE == datatype || DT_INT == datatype || DT_INT64 == datatype)
			{
				for (int i = 0; i < 2; ++i)
				{
					gmpi::ui::Grid lowHighRow(
						  { .gap = lineSpacing(), .auto_rows = rowHeight(), .auto_flow = eAutoFlow::columns, .column_widths = { labelColumnWidth, fr(1.0f) } }
						, {}
					);
					gmpi::ui::Label label(i ? "High" : "Low");
					[[maybe_unused]] auto* view = createParameterFieldEditorView(
						  p
						, 0 == i ? ParameterFieldType::FT_RANGE_LO : ParameterFieldType::FT_RANGE_HI
						, {}
					);
					assert(view);
				}
			}

			// Toggle group (vertical Grid with 3 fixed-height rows)
			{
				gmpi::ui::Grid toggleGroup({ .gap = lineSpacing(), .auto_rows = rowHeight() }, {});
				gmpi::ui::ToggleSwitch t1("Ignore Program Change", p->m_ignoreProgramChange);
				gmpi::ui::ToggleSwitch t2("Private"              , p->isPrivate            );
				gmpi::ui::ToggleSwitch t3("Stateful"             , p->isStateful           );
			}

			// MIDI Automation row (numeric only). Live-updates via MidiAutomationToString:
			// every FT_AUTOMATION write (menu Learn/Unlearn, the MIDI dialog, and the DSP's
			// "lern" message when a controller is detected) calls UpdateGui(FT_AUTOMATION),
			// which routes through setParameter to the converter; text2 repaints on change.
			if (isNumeric((EPlugDataType)datatype))
			{
				gmpi::ui::Grid midiRow(
					  { .gap = lineSpacing(), .auto_rows = rowHeight(), .auto_flow = eAutoFlow::columns, .column_widths = { labelColumnWidth, fr(1.0f) } }
					, {}
				);
				gmpi::ui::Label label("MIDI");

				auto popupMenuView = std::make_unique<gmpi::ui::builder::PopupMenuView>();

				// bind MIDI automation to popup heading text.
				viewModel.more_converters[handleField(p, FT_AUTOMATION)] = std::make_unique<MidiAutomationToString>(popupMenuView->text2, p);

				{
					auto state = std::make_unique< gmpi_forms::State<std::string> >("Learn=1,Unlearn,Edit...");

					popupMenuView->menuItems.setSource(state.get());
					popupMenuView->selfOwnedStates.push_back(std::move(state));
				}

				// handle menu selection
				popupMenuView->onItemSelected = [p](int32_t selectedId)
				{
					p->SetValue(RawView(selectedId), FT_MENU_SELECTION);
				};

				gmpi::ui::builder::ThreadLocalCurrentBuilder->push_back(std::move(popupMenuView));

				// initial value from model.
				p->UpdateGui(FT_AUTOMATION);
			}

			// Trailing spacer separating this parameter from the next.
			gmpi::ui::Spacer trailing({ 0, 0, 0, headingHeight });
		}
	}
}

gmpi::ReturnCode PropertiesBrowser::arrange(const gmpi::drawing::Rect* finalRect)
{
	if (bounds.right - bounds.left != finalRect->right - finalRect->left ||
		bounds.bottom - bounds.top != finalRect->bottom - finalRect->top)
	{
		// Size changed, need to recalculate layout (column widths, etc.)
		viewModel.invalidateView();
	}

	bounds = *finalRect;

	return gmpi::ReturnCode::Ok;
}

float PropertiesBrowser::dividerLineX() const
{
	// Must match Body(): textArea.left = bounds.left + outerMargin; the label column is a
	// fixed px = fraction * textArea width; the line sits in the centre of the column gap.
	const float textAreaLeft = bounds.left + outerMargin;
	const float labelColumnWidth = std::clamp(columnFraction, kMinColumnFraction, kMaxColumnFraction) * propertiesTextAreaWidth(bounds);
	return textAreaLeft + labelColumnWidth + lineSpacing() * 0.5f;
}

bool PropertiesBrowser::columnsShown() const
{
	// Body() only builds the label/value columns (and hence the divider) for a CUG module.
	return dynamic_cast<CUG*>(viewModel.currentModule) != nullptr;
}

gmpi::ReturnCode PropertiesBrowser::onPointerUp(gmpi::drawing::Point point, int32_t flags)
{
	if (draggingDivider)
	{
		draggingDivider = false;

		// End the capture. Form::onPointerUp clears its onMouseMove delta-callback but does
		// not release the host-level pointer capture, so do that here (else HostedView keeps
		// its isCaptured poll armed after the drag).
		const auto rc = Form::onPointerUp(point, flags);
		if (inputhost)
			inputhost->releaseCapture();

		// Persist the new width once, on release — but only if it actually changed (a press
		// in the gap that didn't move shouldn't rewrite the settings file).
		if (app && columnFraction != dividerDragStartFraction)
		{
			app->settings.PropertiesLabelColumnFraction = columnFraction;
			app->SaveSettings();
		}
		return rc;
	}

	return Form::onPointerUp(point, flags);
}

float PropertiesBrowser::currentScrollOffset()
{
	// The ScrollPortal registers its vertical scroll (<= 0, 0 = top) under this key.
	if (auto* s = dynamic_cast<gmpi_forms::State<float>*>(env.findState("ScrollPortal.scroll")))
		return s->get();
	return 0.0f;
}

void PropertiesBrowser::collectRows(gmpi::ui::builder::ViewParent* container)
{
	namespace b = gmpi::ui::builder;
	for (auto& child : container->childViews)
	{
		b::View* v = child.get();

		// Inert rows never highlight: section headings are TextLabelViews, spacers/dividers
		// are Seperators.
		if (dynamic_cast<b::TextLabelView*>(v) || dynamic_cast<b::Seperator*>(v))
			continue;

		// A vertical sub-grid (e.g. the toggle group) is a container of rows, not a row —
		// recurse so each toggle highlights individually. A horizontal grid IS one row.
		if (auto* g = dynamic_cast<b::Grid*>(v); g && g->spec.auto_flow == b::ViewParent::eAutoFlow::rows)
		{
			collectRows(g);
			continue;
		}

		contentRowRects.push_back(v->getBounds());
	}
}

void PropertiesBrowser::collectContentRowRects()
{
	namespace b = gmpi::ui::builder;
	contentRowRects.clear();
	rowHighlightActive = false; // the rows just changed; re-established on the next mouse move

	// Form children are [divider hit target, ScrollPortal]; the rows live under the ScrollPortal.
	for (auto& child : childViews)
	{
		if (auto* sp = dynamic_cast<b::ScrollPortal*>(child.get()))
		{
			collectRows(sp);
			break;
		}
	}
}

void PropertiesBrowser::updateRowHighlight(gmpi::drawing::Point point)
{
	const float scroll = currentScrollOffset();
	const float contentRight = bounds.right - scrollBarWidth; // exclude the scrollbar strip

	bool found = false;
	gmpi::drawing::Rect newRect{};

	if (point.x >= bounds.left && point.x < contentRight)
	{
		// Rows are stored in the portal's content coords; the portal draws them offset by
		// (bounds.left, bounds.top + scroll), so undo that to map the mouse back to a row.
		const float unscrolledY = point.y - bounds.top - scroll;
		for (const auto& r : contentRowRects)
		{
			if (unscrolledY >= r.top && unscrolledY < r.bottom)
			{
				newRect = r;
				found = true;
				break;
			}
		}
	}

	if (found != rowHighlightActive || (found && newRect != hoveredRowRect))
	{
		rowHighlightActive = found;
		hoveredRowRect = newRect;
		redraw();
	}
}

gmpi::ReturnCode PropertiesBrowser::onPointerMove(gmpi::drawing::Point point, int32_t flags)
{
	if (!draggingDivider) // don't chase the highlight while resizing the column
		updateRowHighlight(point);

	return Form::onPointerMove(point, flags);
}

gmpi::ReturnCode PropertiesBrowser::setHover(bool isMouseOverMe)
{
	if (!isMouseOverMe && rowHighlightActive)
	{
		rowHighlightActive = false;
		redraw();
	}
	return Form::setHover(isMouseOverMe);
}

gmpi::ReturnCode PropertiesBrowser::render(gmpi::drawing::api::IDeviceContext* drawingContext)
{
	// Theme change requires full visual rebuild (new colors for backgrounds, text, etc.)
	// The mode comparison below is what decides that, and always was.
	//
	// consumeThemeChanged() used to be called here as well, to beat Form::DoUpdates
	// to the flag. It no longer has to: DoUpdates consumes against its own counter
	// through the versioned overload, so it cannot take anything this view needed.
	// The no-argument overload it used keeps ONE static shared by every caller in
	// the process, so two views watching through it would race in any case.
	const auto currentMode = gmpi::ui::themeModeStorage();
	if (currentMode != viewModel.lastRenderedTheme)
		viewModel.formIsDirty = true;

	// Switching structure <-> panel view for the same module doesn't re-fire OM_SHOW_PROPERTIES,
	// so rebuild when the active view changes — the Layout section is shown only in panel view.
	if (app && app->Document())
	{
		const int32_t viewType = app->Document()->SelectedViewType;
		if (viewType != lastRenderedViewType)
		{
			lastRenderedViewType = viewType;
			viewModel.formIsDirty = true;
		}
	}

	if (viewModel.formIsDirty)
	{
		viewModel.formIsDirty = false;
		viewModel.lastRenderedTheme = currentMode;
		renderVisuals();
		collectContentRowRects(); // the tree was rebuilt — recapture row bounds for the hover highlight
	}

	gmpi::drawing::Graphics g(drawingContext);

	// background color of the properties browser
	g.clear(currentTheme().panelBackground);

	// Row-hover highlight, drawn behind the content: a subtle translucent band over the row
	// under the mouse (content rows only). Blends with the panel background just cleared above.
	if (rowHighlightActive)
	{
		const float scroll = currentScrollOffset();
		// Match the portal's content transform (bounds.left, bounds.top + scroll) so the band
		// lines up with the rows.
		gmpi::drawing::Rect hr{
			hoveredRowRect.left + bounds.left,
			hoveredRowRect.top + bounds.top + scroll,
			hoveredRowRect.right + bounds.left,
			hoveredRowRect.bottom + bounds.top + scroll
		};

		// keep it inside the visible content area (not over the top/bottom margins or scrollbar)
		hr.top = std::max(hr.top, bounds.top);
		hr.bottom = std::min(hr.bottom, bounds.bottom);
		hr.right = std::min(hr.right, bounds.right - scrollBarWidth);

		if (hr.bottom > hr.top)
		{
			const auto tint = (gmpi::ui::themeModeStorage() == gmpi::ui::ThemeMode::Dark)
				? gmpi::drawing::colorFromHex(0xFFFFFFu, 0.06f)   // lighten on dark
				: gmpi::drawing::colorFromHex(0x000000u, 0.05f);  // darken on light
			auto brush = g.createSolidColorBrush(tint);
			g.fillRectangle(hr, brush);
		}
	}

	Form::render(drawingContext);

	// Column divider: a fixed full-height line over the content, marking the shared
	// label|value boundary. Invisible when idle, a light grey while the mouse is over it
	// (matching the scrollbar thumb — the panel's other grabbable chrome).
	if (columnsShown())
	{
		const float x = dividerLineX();
		auto brush = g.createSolidColorBrush(dividerHovered ? currentTheme().scrollbarThumb : gmpi::drawing::Colors::TransparentBlack);
		g.fillRectangle(gmpi::drawing::Rect{ x - 0.5f, bounds.top, x + 0.5f, bounds.bottom }, brush);
	}

	return gmpi::ReturnCode::Ok;
}
