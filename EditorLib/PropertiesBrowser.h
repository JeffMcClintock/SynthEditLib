#pragma once
#include "notify.h"
#include "IPluginGui.h"
#include "experimental/forms.h"
#include "experimental/primatives.h"
#include "experimental/theme.h"
#include "mp_sdk_common.h"
#include "PatchParameter.h"

struct ParamObserver
{
	PatchParameter_base* parameter = {};
	ParameterFieldType fieldType = ParameterFieldType::FT_VALUE;

	ParamObserver(PatchParameter_base* p, ParameterFieldType ft = ParameterFieldType::FT_VALUE)
		: parameter(p), fieldType(ft)
	{
	}

	virtual ~ParamObserver() = default;

	virtual void onParameterChanged(int32_t voice, const void* data, int32_t size) = 0;
};


template <typename T>
struct ParamToState : public ParamObserver
{
	gmpi_forms::State<T> output;

	ParamToState(
		  gmpi_forms::StateRef<T>& dest
		, PatchParameter_base* p
		, ParameterFieldType pFieldType = ParameterFieldType::FT_VALUE
	) : ParamObserver(p, pFieldType)
	{
		// one-way (model -> UI): onParameterChanged drives 'output'; the widget pushes edits
		// back through its own 'validateAndSave' back-channel, so there is no UI -> model observer.
		dest.setSource(&output);
	}

	void onParameterChanged(int32_t voice, const void* data, int32_t size)
	{
		output.set(RawToValue<T>(data, size));
	}
};

// one-way binding from a parameter field's value to its display string (model -> UI).
// The datatype (hence formatter) is captured once at bind time, and onParameterChanged
// formats the raw 'data' it is handed — no per-change datatype re-derivation or value re-fetch.
struct ParamToText : public ParamObserver
{
	gmpi_forms::State<std::string> output;
	int datatype{};

	ParamToText(
		  gmpi_forms::StateRef<std::string>& dest
		, PatchParameter_base* p
		, ParameterFieldType pFieldType
		, int pDatatype
	) : ParamObserver(p, pFieldType), datatype(pDatatype)
	{
		dest.setSource(&output);
	}

	void onParameterChanged(int32_t voice, const void* data, int32_t size) override;
};

// one-way binding from parameter MIDI automation type to string description
struct MidiAutomationToString : public ParamObserver
{
	gmpi_forms::State<std::string> output;

	MidiAutomationToString(
		gmpi_forms::StateRef<std::string>& dest
		, PatchParameter_base* p
	) : ParamObserver(p, ParameterFieldType::FT_AUTOMATION)
	{
		dest.setSource(&output);
	}

	void onParameterChanged(int32_t voice, const void* data, int32_t size);
};


template <typename FROM, typename TO>
struct StateTypeConverter : public gmpi_forms::thing
{
	gmpi_forms::StateRef<FROM> from;
	gmpi_forms::State<TO> to;

	std::function<TO(const FROM&)> convertForward;

	StateTypeConverter()
	{
		from.addObserver([this]()
			{
				to = convertForward(from.get());
			}
		);
	}

	// Initial model -> UI sync. Must be called after 'from' (its source) and convertForward are
	// set up — the constructor can't do it, as neither is ready at construction time.
	// Bindings are one-way: the widget's 'validateAndSave' back-channel handles UI -> model, so
	// there is no reverse wiring.
	void init()
	{
		to = convertForward(from.get());
	}
};

// This bridges the Module from the SE notification system (Notifiable) with the GMPI-UI system (IObservableObject)
struct PropertiesViewModel : public gmpi_forms::IObservableObject, public Notifiable, public gmpi::api::IParameterObserver
{
	CDocOb* currentModule = {};
	class CPatchManager* currentPatchManager = {};
	bool formIsDirty = true;
	gmpi::ui::ThemeMode lastRenderedTheme = gmpi::ui::ThemeMode::Dark;
	std::map<int64_t, std::unique_ptr<ParamObserver> > more_converters;

	// Layout section: one-way (model -> UI) States mirroring the current module's panel rect
	// as X/Y/W/H display strings. Kept fresh by watching the module's container for
	// OM_ONCHANGE_CHILD_POSITION_PANEL; the widgets bind these directly and push edits back
	// through their own validateAndSave back-channel (no reverse observer). Living here (not
	// in the Body-built view tree) they survive form rebuilds.
	gmpi_forms::State<std::string> layoutX, layoutY, layoutW, layoutH;
	Notifier* layoutContainer = {}; // the container currently watched for panel-rect changes

	// re-seed the layout States from the current module's panel rect.
	void refreshLayoutStates();

	// Per-pin name States for renamable pins, re-seeded on the container's OM_NAME_CHANGE
	// (a rename can come from this form, another view, or a tied IO pin). Populated by
	// Body(), cleared on each rebuild. Keys are lookup-only — a deleted pin's stale entry
	// is never dereferenced (refresh iterates the module's live Plugs list).
	std::map<class IPlug*, gmpi_forms::State<std::string>> pinNameStates;

	// re-seed every mapped pin-name State from the model.
	void refreshPinNameStates();

	~PropertiesViewModel();

	// invalidate the entire properties browser and repaint ('ObjectWillChange') to trigger the re-render.
	void invalidateView()
	{
		formIsDirty = true;
		ObjectWillChange();
	}

	void OnNotify(Notifier* sender, int lHint, void* pHint = 0) override;

	// IParameterObserver
	gmpi::ReturnCode setParameter(int32_t parameterHandle, gmpi::Field fieldId, int32_t voice, int32_t size, const uint8_t* data) override;

	GMPI_QUERYINTERFACE_METHOD(gmpi::api::IParameterObserver);
	GMPI_REFCOUNT_NO_DELETE
};


class PropertiesBrowser : public gmpi::ui::Form, public gmpi_forms::IObserver
{
	PropertiesViewModel viewModel;

	// BACKLOG E61 -- HAS THE MODULE A Body() LAMBDA CAPTURED OUTLIVED THE PANE'S
	// VIEW OF IT?
	//
	// Body()'s commit lambdas capture the module (or one of its pins) as a RAW
	// POINTER, and a native text edit can outlive both. On macOS
	// GMPI_MAC_TextEdit::showAsync calls addRef() on ITSELF -- "self-extend
	// lifetime" -- and ~GMPI_MAC_TextEdit only removes the NSTextField from its
	// superview; it never calls dismissTextField. So tearing this pane's widgets
	// down does NOT dismiss an open field. It keeps first responder, and when the
	// user clicks away Cocoa sends textDidEndEditing, which commits into a lambda
	// whose captured module was freed some time earlier.
	//
	// That is the crash Jeff hit: delete a container while its child's pin-name
	// field is open, then finish the edit --
	//   PropertiesBrowser::Body()::$_13 <- GMPI_MAC_TextEdit::dismissTextField
	//   <- -[NSTextField textDidEndEditing:]
	// EXC_BAD_ACCESS with a pointer-authentication failure, which is the
	// signature of a call through freed memory rather than through a null.
	//
	// COMPARING IS SAFE WHERE DEREFERENCING IS NOT. This only ever compares
	// addresses; it never touches `captured`. OM_DELETE sets currentModule to
	// nullptr at the last moment the module is still valid (SynthEditLib#64), so
	// a freed module can no longer equal it and every commit lambda declines.
	// #64's guard and this one are the same fix seen from two ends, which is why
	// "the pane still shows the dead child" was never merely cosmetic.
	//
	// THE RESIDUAL, STATED BECAUSE IT IS REAL: if the allocator hands the same
	// address to a NEW module and that module becomes the one on show, this would
	// wrongly allow the commit. Closing that needs a re-resolvable handle rather
	// than a pointer, and EditorLib has no handle->module lookup today. Recorded
	// on E61 rather than left implicit.
	bool moduleStillShown(const void* captured) const
	{
		return captured && static_cast<const void*>(viewModel.currentModule) == captured;
	}

	gmpi::drawing::Rect bounds = {};

	// Tracks which module was last rendered, so OnModelWillChange can reset
	// the scroll position only when the displayed module actually changes
	// (not on resize, theme change, or parameter updates).
	CDocOb* lastRenderedModule = {};

	// Which editor view (CF_PANEL_VIEW / CF_STRUCTURE_VIEW) was active last render. A view
	// switch for the same module doesn't re-fire OM_SHOW_PROPERTIES, so render() watches this
	// to rebuild when it changes (the Layout section is panel-view only). -1 = not yet seen.
	int32_t lastRenderedViewType = -1;

	CSynthEditAppBase* app = {};

	// Label-column width as a fraction (0..1) of the text area, shared by every row so
	// the value editors line up. Persisted in app->settings; adjusted by dragging the
	// column divider. Survives the frequent Body() rebuilds (it's a plain member).
	float columnFraction = 0.4f;

	// Divider drag/hover state. The divider is a full-height line drawn in render() and
	// driven by the pointer overrides below (not a competing mouse target). A press near the
	// line gives the content first refusal, so a click on a widget the line crosses still
	// reaches that widget; the drag only starts when nothing under the point handled it.
	bool draggingDivider = false;
	bool dividerHovered = false;
	float dividerDragStartFraction = 0.4f;

	// Window x of the divider line, derived from columnFraction + bounds (matches Body()).
	float dividerLineX() const;
	// True when a module's label/value columns (hence the divider) are being shown.
	bool columnsShown() const;

	// Row-hover highlight. A subtle band is drawn behind the row under the mouse — but only
	// over "content" rows (label/value pairs, the name field, toggles), never over inert rows
	// (headings, spacers/dividers). Rects are the laid-out row bounds in unscrolled content
	// coords, gathered from the view tree after each rebuild; the scroll offset is applied at
	// draw/hit time. Pure drawing (no mouse targets), so it doesn't disturb the divider.
	std::vector<gmpi::drawing::Rect> contentRowRects;
	bool rowHighlightActive = false;
	gmpi::drawing::Rect hoveredRowRect = {};

	float currentScrollOffset();
	void collectContentRowRects();
	void collectRows(gmpi::ui::builder::ViewParent* container);
	void updateRowHighlight(gmpi::drawing::Point point);

	// pushes the editor view to the current builder (gmpi::ui::builder::ThreadLocalCurrentBuilder).
	// returns the (non-owning) view pointer so callers can branch on whether anything was created
	// (e.g. for the optional file-browse button next to filename editors).
	gmpi::ui::builder::View* createParameterFieldEditorView(
		  class PatchParameter_base* p
		, ParameterFieldType fieldType
		, gmpi::drawing::Rect bounds
	);

public:
	 PropertiesBrowser(CSynthEditAppBase* p_app);
	~PropertiesBrowser();

	void Init();
	void Body() override;

	// IObserver
	void OnModelWillChange() override;

	// Column-divider drag: the divider's (lower-priority) mouse target starts a captured
	// drag; its release surfaces here so the new width can be persisted. Hover and hit
	// priority are handled entirely by that target, so only pointer-up needs overriding.
	gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override;

	// Track the mouse to move the row-hover highlight, and clear it when the mouse leaves.
	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override;
	gmpi::ReturnCode setHover(bool isMouseOverMe) override;

	gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override;
	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override;

	// IMpGraphics4 interface
	gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override
	{
		*returnRect = bounds;
		return gmpi::ReturnCode::Ok;
	}
};
