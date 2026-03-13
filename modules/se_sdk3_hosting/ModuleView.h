#pragma once

#ifdef _WIN32
#include <dwrite.h>
#include "modules/se_sdk3_hosting/DirectXGfx.h"
#endif

#include "./IViewChild.h"
#include <array>
#include <string>
#include <memory>
#include <unordered_map>
#include "mp_sdk_gui2.h"
#include "Extensions/EmbeddedFile.h"

#include "Core/GmpiSdkCommon.h"
#include "Core/GmpiApiEditor.h"
#include "helpers/NativeUi.h"

#include "modules/se_sdk3_hosting/GmpiResourceManager.h"
#include "plug_description.h"
#include "RawView.h"

class Module_Info;
class cpu_accumulator;

namespace SE2
{
class IPresenter;
class IViewChild;
class Sdk3Helper;
}

class DECLSPEC_NOVTABLE ISubView : public gmpi::api::IUnknown
{
public:
	virtual void OnCableDrag(SE2::ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistance, SE2::ModuleView*& bestModule, int& bestPinIndex) = 0;
	virtual bool hitTest(int32_t flags, gmpi::drawing::Point* point) = 0;
	virtual bool MP_STDCALL isVisible() = 0;
	virtual void process() = 0;

	// {4F6B4050-F169-401C-AAEB-D6057ECDF58E}
	inline static const gmpi::api::Guid guid =
	{ 0x4f6b4050, 0xf169, 0x401c, { 0xaa, 0xeb, 0xd6, 0x5, 0x7e, 0xcd, 0xf5, 0x8e } };
};

namespace SE2
{
	class ResizeAdorner;
	class ResizeAdornerStructure;

	template< class T>
	class GraphicsResourceCache
	{
		std::unordered_map< gmpi::drawing::api::IFactory*, std::weak_ptr<T> > resourceStructs;

	public:
		std::shared_ptr<T> get(gmpi::drawing::Graphics& g)
		{
			auto f = g.getFactory();
	        auto factory = gmpi::drawing::AccessPtr::get(f);
			auto resourcePtr = resourceStructs[factory].lock();

			if(!resourcePtr)
			{
				resourcePtr = std::make_shared<T>(g);
				resourceStructs[factory] = resourcePtr;
			}

			return resourcePtr;
		}
		std::shared_ptr<T> get(gmpi::drawing::Factory& factory)
		{
			auto resourcePtr = resourceStructs[gmpi::drawing::AccessPtr::get(factory)].lock();

			if(!resourcePtr)
			{
				resourcePtr = std::make_shared<T>(factory);
				resourceStructs[gmpi::drawing::AccessPtr::get(factory)] = resourcePtr;
			}

			return resourcePtr;
		}
	};

	// adapts a GMPI-UI plugin to a moduleview
	class GmpiHelper :
		  public gmpi::api::IInputHost
		, public gmpi::api::IDialogHost
		, public gmpi::api::IEditorHost
		, public gmpi::api::IEditorHost2
		, public gmpi::api::IDrawingHost
		, public gmpi::api::IParameterSetter
		, public synthedit::IEmbeddedFileSupport
	{
		class ModuleView& moduleview;
	public:
		GmpiHelper(ModuleView& pmoduleview) : moduleview(pmoduleview) {}

		// IInputHost
		gmpi::ReturnCode setCapture() override;
		gmpi::ReturnCode getCapture(bool& returnValue) override;
		gmpi::ReturnCode releaseCapture() override;

		// IEditorHost
		gmpi::ReturnCode setPin(int32_t pinId, int32_t voice, int32_t size, const uint8_t* data) override;
		int32_t getHandle() override;
		// IDrawingHost
		gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory) override;
		void invalidateRect(const gmpi::drawing::Rect* invalidRect) override;
		void invalidateMeasure() override;
		float getRasterizationScale() override;
		// IDialogHost
		gmpi::ReturnCode createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit) override;
		gmpi::ReturnCode createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) override;
		gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener) override; // why here not IInputHost? becuase it is effectivly an invisible text-edit
		gmpi::ReturnCode createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override;
		gmpi::ReturnCode createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override;

		// IParameterSetter_x
		gmpi::ReturnCode getParameterHandle(int32_t moduleParameterId, int32_t& returnHandle) override;
		gmpi::ReturnCode setParameter(int32_t parameterHandle, gmpi::Field fieldId, int32_t voice, int32_t size, const uint8_t* data) override;

		// IEmbeddedFileSupport
		gmpi::ReturnCode findResourceUri(const char* fileName, /*const char* resourceType,*/ gmpi::api::IString* returnFullUri) override;
		gmpi::ReturnCode registerResourceUri(const char* fullUri) override;
		gmpi::ReturnCode openUri(const char* fullUri, gmpi::api::IUnknown** returnStream) override;
		gmpi::ReturnCode clearResourceUris() override;

		// IEditorHost2_x
		gmpi::ReturnCode setDirty() override;

		gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
		{
			*returnInterface = {};
			GMPI_QUERYINTERFACE(gmpi::api::IInputHost);
			GMPI_QUERYINTERFACE(gmpi::api::IDialogHost);
			GMPI_QUERYINTERFACE(gmpi::api::IEditorHost);
			GMPI_QUERYINTERFACE(gmpi::api::IEditorHost2);		
			GMPI_QUERYINTERFACE(gmpi::api::IDrawingHost);
			GMPI_QUERYINTERFACE(gmpi::api::IParameterSetter);
			GMPI_QUERYINTERFACE(synthedit::IEmbeddedFileSupport);
			return gmpi::ReturnCode::NoSupport;
		}
		GMPI_REFCOUNT
	};

	// adapt a legacy (SDK3) module to moduleview
	class Sdk3Helper :
		  public gmpi::IMpUserInterfaceHost2
		, public gmpi::IMpUserInterfaceHost
		, public gmpi_gui::IMpGraphicsHost
	{
		friend class PatchCableChangeNotifier;

	protected:
		class ModuleView& moduleview;

	public:
		explicit Sdk3Helper(ModuleView& pmoduleview) : moduleview(pmoduleview) {}
		virtual ~Sdk3Helper() = default;

		void OnPatchCablesUpdate(RawView patchCablesRaw); // from PatchCableChangeNotifier

		// IMpGraphicsHost
		int32_t GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory) override;
		void invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect) override;
		void invalidateMeasure() override {}
		int32_t setCapture() override;
		int32_t getCapture(int32_t& returnValue) override;
		int32_t releaseCapture() override;
		int32_t createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override;
		int32_t createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog) override;
		int32_t createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override;
		int32_t createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override;

		// IMpUserInterfaceHost2
		int32_t pinTransmit(int32_t pinId, int32_t size, const void* data, int32_t voice) override;
		int32_t sendMessageToAudio(int32_t id, int32_t size, const void* messageData) override;
		int32_t getHandle(int32_t& returnValue) override;
		int32_t createPinIterator(gmpi::IMpPinIterator** returnIterator) override;
		int32_t ClearResourceUris() override;
		int32_t RegisterResourceUri(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override;
		int32_t FindResourceU(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override;
		int32_t LoadPresetFile_DEPRECATED(const char* presetFilePath) override;
		int32_t OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream) override;

		// IMpUserInterfaceHost
		int32_t pinTransmit(int32_t pinId, int32_t size, void* data, int32_t voice = 0) override;
		int32_t sendMessageToAudio(int32_t id, int32_t size, void* messageData) override;
		int32_t setIdleTimer(int32_t active) override;
		int32_t getHostId(int32_t maxChars, wchar_t* returnString) override;
		int32_t getHostVersion(int32_t& returnValue) override;
		int32_t resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename) override;
		int32_t addContextMenuItem(wchar_t* menuText, int32_t index, int32_t flags) override;
		int32_t getPinCount(int32_t& returnCount) override;
		int32_t openProtectedFile(const wchar_t* shortFilename, gmpi::IProtectedFile** file) override;

		int32_t queryInterface(const gmpi::MpGuid& iid, void** object) override;
		GMPI_REFCOUNT
	};

	class ModuleView : public ViewChild
	{
	protected:
		Module_Info* moduleInfo = {};

		std::unique_ptr<GmpiHelper> gmpiHelper;
		std::unique_ptr<Sdk3Helper> sdk3Helper;
		
	public:
		static const int SelectionFrameOffset = 1;
		static const int ResizeHandleRadius = 3;

		bool initialised_;
		std::vector<int> inputPinIds;
		bool ignoreMouse;
		int SortOrder = -1;

		ModuleView(const wchar_t* typeId, ViewBase* pParent, int handle);
		ModuleView(Json::Value* context, ViewBase* pParent);
		~ModuleView() = default;

		Module_Info* getModuleType()
		{
			return moduleInfo;
		}

		std::string name;

		void BuildContainer(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap);
		void BuildContainerCadmium(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap);
		void Build();
		void queryPluginInterfaces(gmpi::shared_ptr<gmpi::api::IUnknown>& object);
		void CreateGraphicsResources();
		virtual gmpi::drawing::PathGeometry getOutline(gmpi::drawing::Factory drawingFactory);
		bool isMonoDirectional() const
		{
			return !pluginEditor2.isNull();
		}

		// unidirectional modules
		virtual void setDirty() override { dirty = true; }
		virtual bool getDirty() override
		{
			return dirty;
		}
		void process() override;
#if 0
		// IMpGraphicsHost support.
		int32_t GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory) override;
		virtual void invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect) override;
		int32_t setCapture() override;
		int32_t getCapture(int32_t& returnValue) override;
		int32_t releaseCapture() override;

		// IMpGraphicsHostBase support.
		int32_t createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override;
		int32_t createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog) override;
		int32_t createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override;
		int32_t createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override;
#endif

		void AddConnection(int myPinIndex, ModuleView* otherModule, int otherModulePinIndex)
		{
			connections_.insert({ myPinIndex, connection(myPinIndex, otherModule, otherModulePinIndex) });
			//_RPT2(0, "m:%d AddConnection pin: %d\n", handle, myPinIndex);
		}
		void setTotalPins(int totalPins)
		{
			totalPins_ = totalPins;
		}
#if 0

		// IMpUserInterfaceHost2 support
		// Plugin UI updates a parameter.
		int32_t pinTransmit(int32_t pinId, int32_t size, const void* data, int32_t voice) override
		{
			auto it = connections_.find(pinId);
			while (it != connections_.end() && (*it).first == pinId)
			{
				auto& connection = (*it).second;
				connection.otherModule_->setPin(this, pinId, connection.otherModulePinIndex_, voice, size, data);
				it++;
			}

			if (!initialised_)
			{
				//_RPT2(0, "m:%d alreadySentDataPins_ <- %d\n", handle, pinId);
				alreadySentDataPins_.push_back(pinId);
			}

			// input GUI pins also echo value back into plugin.
			if (std::find(inputPinIds.begin(), inputPinIds.end(), pinId) != inputPinIds.end() && recursionStopper_ < 10)
			{
				++recursionStopper_;

				// Notify myself
				if (pluginParameters)
				{
					// didn't actual notify (because value is already set)
					pluginParameters->setPin(pinId, voice, size, data);

					if (pluginParameters2B)
					{
						pluginParameters2B->notifyPin(pinId, voice);
					}
				}
				else
				{
					if (pluginParametersLegacy)
					{
		// already set, only needs notify.				pluginParametersLegacy->setPin(pinId, voice, size, (void*)data);
						pluginParametersLegacy->notifyPin(pinId, voice);
					}
				}

				--recursionStopper_;
			}

			return gmpi::MP_OK;
		}

		// Back door to Audio class. Not recommended. Use Parameters instead to support proper automation.
		int32_t sendMessageToAudio(int32_t id, int32_t size, const void* messageData) override;

		// Each plugin instance has unique handle shared by UI and Audio class.
		int32_t getHandle(int32_t& returnValue) override;

		// Get information about UI's pins.
		int32_t createPinIterator(gmpi::IMpPinIterator** returnIterator) override
		{
			return gmpi::MP_FAIL;
		}

		int32_t ClearResourceUris() override
		{
			return gmpi::MP_OK;
		}

		int32_t RegisterResourceUri(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override;

		int32_t FindResourceU(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override;

		int32_t LoadPresetFile_DEPRECATED(const char* presetFilePath) override;

		int32_t OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream) override // returns an IProtectedFile.
		{
			return GmpiResourceManager::Instance()->OpenUri(fullUri, returnStream);
		}

		// IMpUserInterfaceHost (legacy host) support.
		int32_t pinTransmit(int32_t pinId, int32_t size, /*const*/ void* data, int32_t voice = 0) override;
		int32_t sendMessageToAudio(int32_t id, int32_t size, /*const*/ void* messageData) override;
		int32_t setIdleTimer(int32_t active) override;
		int32_t getHostId(int32_t maxChars, wchar_t* returnString) override;
		int32_t getHostVersion(int32_t& returnValue) override;
		int32_t resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename) override;
		int32_t addContextMenuItem( /*const*/ wchar_t* menuText, int32_t index, int32_t flags) override;
		int32_t getPinCount(int32_t& returnCount) override;
		int32_t openProtectedFile(const wchar_t* shortFilename, gmpi::IProtectedFile **file) override;


		// IUnknown methods
		int32_t queryInterface(const gmpi::MpGuid& iid, void** object) override
		{
			if (iid == gmpi::MP_IID_UI_HOST2
				|| iid == gmpi::MP_IID_UI_HOST
				|| iid == gmpi_gui::SE_IID_GRAPHICS_HOST
				|| iid == gmpi_gui::SE_IID_GRAPHICS_HOST_BASE)
			{
				if (sdk3Helper)
					return sdk3Helper->queryInterface(iid, object);

				if (iid == gmpi::MP_IID_UI_HOST2)
				{
					*object = reinterpret_cast<IMpUnknown*>(static_cast<IMpUserInterfaceHost2*>(this));
					addRef();
					return gmpi::MP_OK;
				}
				if (iid == gmpi::MP_IID_UI_HOST)
				{
					*object = reinterpret_cast<IMpUnknown*>(static_cast<IMpUserInterfaceHost*>(this));
					addRef();
					return gmpi::MP_OK;
				}
				if (iid == gmpi_gui::SE_IID_GRAPHICS_HOST || iid == gmpi_gui::SE_IID_GRAPHICS_HOST_BASE)
				{
					*object = reinterpret_cast<IMpUnknown*>(static_cast<IMpGraphicsHost*>(this));
					addRef();
					return gmpi::MP_OK;
				}
			}

			*object = nullptr;
			return gmpi::MP_NOSUPPORT;
		}
#endif

		gmpi::IMpUserInterface2* getpluginParameters()
		{
			return pluginParameters;
		}

		void initialize();

		virtual int32_t setPin(ModuleView* fromModule, int32_t fromPinId, int32_t pinId, int32_t voice, int32_t size, const void* data);

		bool isPinConnected(int pinIndex)
		{
			return connections_.find(pinIndex) != connections_.end();
		}

		bool isPinConnectionActive(int pinIndex) const;

		// IMouseCaptureObect
#if 0 // def SE_TAR GET_PURE_UWP
		virtual void OnPointerPressed(float x, float y, Windows::UI::Xaml::Input::PointerRoutedEventArgs ^e) override;
		virtual void OnPointerMoved(float x, float y, Windows::UI::Xaml::Input::PointerRoutedEventArgs ^e) override;
		virtual void OnPointerReleased(float x, float y, Windows::UI::Xaml::Input::PointerRoutedEventArgs ^e) override;
#endif
		// IViewChild.
		gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode setHover(bool mouseIsOverMe) override;

		gmpi::drawing::Matrix3x2 OffsetToClient()
		{
			return gmpi::drawing::makeTranslation(-bounds_.left - pluginGraphicsPos.left, -bounds_.top - pluginGraphicsPos.top);
		}

		gmpi::drawing::Point PointToPlugin(gmpi::drawing::Point point)
		{
			return gmpi::drawing::transformPoint(OffsetToClient(), point);
		}

		gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override;

		gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override;
#if 0// todo update these
		int32_t vc_onContextMenu(int32_t idx) override;
#endif

		std::string getToolTip(gmpi::drawing::Point point) override;
		void receiveMessageFromAudio(void*) override;

		void OnCableDrag(ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistance, ModuleView*& bestModule, int& bestPinIndex) override;
		gmpi::drawing::Point getConnectionPoint(CableType cableType, int pinIndex) override;
		std::vector<patchpoint_description>* getPatchPoints();

		void OnMoved(gmpi::drawing::Rect& newRect) override;
		void OnNodesMoved(std::vector<gmpi::drawing::Point>& newNodes) override {}
		virtual std::unique_ptr<SE2::IViewChild> createAdorner(ViewBase* pParent) = 0;
		virtual bool isMuted()
		{
			return false;
		}

		virtual void OnCpuUpdate(cpu_accumulator* cpuInfo) {}
		virtual void SetHoverScopeText(const char* text) {}
		virtual void SetHoverScopeWaveform(std::unique_ptr< std::vector<float> > data) {}

		// SDK3
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface> pluginParametersLegacy;
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2> pluginParameters;
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2B> pluginParameters2B;
		gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics> pluginGraphics;
		gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics2> pluginGraphics2;
		gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> pluginGraphics3;
		gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics4> pluginGraphics4; // includes pluginGraphics3

		// 'real' GMPI
		gmpi::shared_ptr<gmpi::api::IEditor> pluginParameters_GMPI;
		gmpi::shared_ptr<gmpi::api::IInputClient> pluginInput_GMPI;
		gmpi::shared_ptr<gmpi::api::IDrawingClient> pluginGraphics_GMPI;
		gmpi::shared_ptr<gmpi::api::IEditor2> pluginEditor2;

		// SubView
		gmpi::shared_ptr<ISubView> subView;
		gmpi::drawing::Rect pluginGraphicsPos;

		// GUI connections.
		struct connection
		{
			connection(int myPinIndex, class ModuleView* otherModule, int otherModulePinIndex) : otherModule_(otherModule), myPinIndex_(myPinIndex), otherModulePinIndex_(otherModulePinIndex) {}
			class ModuleView* otherModule_;
			int myPinIndex_;
			int otherModulePinIndex_;
		};
		int recursionStopper_;
		std::multimap<int, connection> connections_;
		int totalPins_ = -1; // only supplied for autoduplicating modules.

		// While connections are being made, note which pins already sent data.
		std::vector<int> alreadySentDataPins_;

		bool mouseCaptured = false;
		bool dirty{ true };
		virtual bool isRackModule() = 0;
		void preDelete() override;

//		GMPI_REFCOUNT_NO_DELETE;
	};

	struct pinViewInfo
	{
		std::string name;
		int indexCombined; // Numbers both GUI and DSP pins into one long list.
		int plugDescID;
		char direction;
		char datatype;
		bool isGuiPlug;
		bool isVisible;
		bool isIoPlug;
		bool isAutoduplicatePlug;
		bool isTiedToUnconnected;
	};

	struct sharedGraphicResources_struct
	{
		inline static const int plugTextSize = 10;
		inline static const int plugDiameter = 12;
		inline static constexpr std::array<std::array<uint32_t, 2>, 15> pinColors =
		{{
			//  inner      outline
			{0x00BB00u, 0x008C00u}, // ENUM green
			{0xFF0000u, 0xBF0000u}, // TEXT red
			{0xFFCC00u, 0xBF9900u}, // MIDI2 yellow
			{0x00BCBCu, 0x00BCBCu}, // DOUBLE
			{0x555555u, 0x404040u}, // BOOL - grey.
			{0x0044FFu, 0x0033BFu}, // float-audio blue
			{0x00CCEEu, 0x0099B3u}, // FLOAT green-blue
			{0x008989u, 0x008989u}, // unused
			{0xFF8800u, 0xBF6600u}, // INT orange
			{0xFF8800u, 0xBF6600u}, // INT64 orange
			{0xFF55FFu, 0xBF40BFu}, // BLOB -purple
			{0xFF55FFu, 0xBF40BFu}, // Class -purple
			{0xFF0000u, 0xBF0000u}, // string (utf8) red
			{0xFF55FFu, 0xBF40BFu}, // BLOB2 -purple
			{0xFFFFFFu, 0x808080u}, // Spare - white.
		}};

		sharedGraphicResources_struct(gmpi::drawing::Factory& factory)
		{
			const char* pinFontfamily = "Verdana";
			const std::array<std::string_view, 1> pinFontFamilies{ pinFontfamily };

			// Left justified text.
			tf_plugs_left = factory.createTextFormat(static_cast<float>(plugTextSize), pinFontFamilies); // see also measure
			tf_plugs_left.setLineSpacing(static_cast<float>(plugDiameter), static_cast<float>(plugDiameter - 2)); // squish up text a bit.
			tf_plugs_left.setTextAlignment(gmpi::drawing::TextAlignment::Leading);

			// Right justified text.
			tf_plugs_right = factory.createTextFormat(static_cast<float>(plugTextSize), pinFontFamilies);
			tf_plugs_right.setLineSpacing(static_cast<float>(plugDiameter), static_cast<float>(plugDiameter - 2)); // squish up text a bit.
			tf_plugs_right.setTextAlignment(gmpi::drawing::TextAlignment::Trailing);

			// Header.
			tf_header = factory.createTextFormat(static_cast<float>(plugTextSize) + 2.0f);
			tf_header.setTextAlignment(gmpi::drawing::TextAlignment::Center);
		}

		~sharedGraphicResources_struct()
		{
			// _RPT0(0, "~sharedGraphicResources_struct\n");
		}

		void initializePinFillBrushes(gmpi::drawing::Graphics& g)
		{
			if (pinBrushesInitialized)
				return;

			moduleOutlineBrush = g.createSolidColorBrush(gmpi::drawing::colorFromHex(0x7C7C7Cu));
			moduleOutlineBrushHovered = g.createSolidColorBrush(gmpi::drawing::Colors::DodgerBlue);

			for (size_t i = 0; i < pinColors.size(); ++i)
			{
				const auto fillColor = gmpi::drawing::colorFromHex(pinColors[i][0]);
				pinFillBrushes[i] = g.createSolidColorBrush(fillColor);

				const auto hoveredFillColor = gmpi::drawing::Color(
					fillColor.r + (1.0f - fillColor.r) * 0.35f,
					fillColor.g + (1.0f - fillColor.g) * 0.35f,
					fillColor.b + (1.0f - fillColor.b) * 0.35f,
					fillColor.a + (1.0f - fillColor.a) * 0.35f
				);
				pinFillBrushesHovered[i] = g.createSolidColorBrush(hoveredFillColor);

				const auto outlineColor = gmpi::drawing::colorFromHex(pinColors[i][1]);
				pinOutlineBrushes[i] = g.createSolidColorBrush(outlineColor);

				const auto hoveredOutlineColor = gmpi::drawing::Color(
					outlineColor.r + (1.0f - outlineColor.r) * 0.25f,
					outlineColor.g + (1.0f - outlineColor.g) * 0.25f,
					outlineColor.b + (1.0f - outlineColor.b) * 0.25f,
					outlineColor.a + (1.0f - outlineColor.a) * 0.25f
				);
				pinOutlineBrushesHovered[i] = g.createSolidColorBrush(hoveredOutlineColor);
			}

			pinBrushesInitialized = true;
		}

		gmpi::drawing::TextFormat tf_plugs_left;
		gmpi::drawing::TextFormat tf_plugs_right;
		gmpi::drawing::TextFormat tf_header;
		std::array<gmpi::drawing::SolidColorBrush, pinColors.size()> pinFillBrushes;
		std::array<gmpi::drawing::SolidColorBrush, pinColors.size()> pinFillBrushesHovered;
		std::array<gmpi::drawing::SolidColorBrush, pinColors.size()> pinOutlineBrushes;
		std::array<gmpi::drawing::SolidColorBrush, pinColors.size()> pinOutlineBrushesHovered;
		gmpi::drawing::SolidColorBrush moduleOutlineBrush;
		gmpi::drawing::SolidColorBrush moduleOutlineBrushHovered;
		bool pinBrushesInitialized = false;

		std::unordered_map< std::string, gmpi::drawing::PathGeometry > outlineCache;
	};

	class ModuleViewPanel : public ModuleView
	{
		bool isRackModule_ = {};

	public:
		ModuleViewPanel(const wchar_t* typeId, ViewBase* pParent, int handle);
		ModuleViewPanel(Json::Value* context, class ViewBase* pParent, std::map<int, class ModuleView*>& guiObjectMap);
		virtual void render(gmpi::drawing::Graphics& g) override;
		virtual void measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize) override;
		virtual void arrange(gmpi::drawing::Rect finalRect) override;
		gmpi::drawing::Rect getClipArea() override;

		bool EndCableDrag(gmpi::drawing::Point point, ConnectorViewBase* dragline, int32_t keyFlags) override;

		bool isVisable() override
		{
			return pluginGraphics || pluginGraphics_GMPI;
		}
		gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override;
		bool hitTestR(int32_t flags, gmpi::drawing::Rect selectionRect) override;
		float hitTestFuzzy(int32_t flags, gmpi::drawing::Point point) override
		{
			return hitTest(point, flags) == gmpi::ReturnCode::Ok ? 0.0f : 1000.0f;
		}

		bool isShown() override;
		bool isDraggable(bool editEnabled) override;

		std::unique_ptr<SE2::IViewChild> createAdorner(ViewBase* pParent) override;
		bool isRackModule() override
		{
			return isRackModule_;
		}
//		void invalidateMeasure() override {}
	};
}
