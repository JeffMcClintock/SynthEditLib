#pragma once

#ifdef _WIN32
#include <dwrite.h>
#include "modules/se_sdk3_hosting/DirectXGfx.h"
#endif

#include "./IViewChild.h"
#include <chrono>
#include <string>
#include <memory>
#include <unordered_map>
#include "modules/se_sdk3/mp_sdk_common.h"
#include "modules/se_sdk2/se_datatypes.h"
#include "modules/se_sdk3/se_mp_extensions.h"
#include "Extensions/EmbeddedFile.h"

#include "GmpiSdkCommon.h"
#include "GmpiApiEditor.h"
#include "helpers/NativeUi.h"

#include "modules/se_sdk3_hosting/GmpiResourceManager.h"
#include "plug_description.h"

class Module_Info;
class cpu_accumulator;

namespace SE2
{
class IPresenter;
class IViewChild;
}

class DECLSPEC_NOVTABLE ISubView : public gmpi::IMpUnknown
{
public:
	virtual void OnCableDrag(SE2::ConnectorViewBase* dragline, GmpiDrawing::Point dragPoint, float& bestDistance, SE2::IViewChild*& bestModule, int& bestPinIndex) = 0;
	virtual bool hitTest(int32_t flags, GmpiDrawing_API::MP1_POINT* point) = 0;
	virtual bool MP_STDCALL isVisible() = 0;
	virtual void process() = 0;
};

// GUID for ISubView
static const gmpi::MpGuid SE_IID_SUBVIEW =
// {4F6B4050-F169-401C-AAEB-D6057ECDF58E}
{ 0x4f6b4050, 0xf169, 0x401c, { 0xaa, 0xeb, 0xd6, 0x5, 0x7e, 0xcd, 0xf5, 0x8e } };

namespace SE2
{
	class ResizeAdorner;
	class ResizeAdornerStructure;

	template< class T>
	class GraphicsResourceCache
	{
		std::unordered_map< GmpiDrawing_API::IMpFactory*, std::weak_ptr<T> > resourceStructs;

	public:
		std::shared_ptr<T> get(GmpiDrawing::Graphics& g)
		{
	        auto factory = g.GetFactory().Get();
			auto resourcePtr = resourceStructs[factory].lock();

			if(!resourcePtr)
			{
				resourcePtr = std::make_shared<T>(g);
				resourceStructs[factory] = resourcePtr;
			}

			return resourcePtr;
		}
		std::shared_ptr<T> get(GmpiDrawing::Factory& factory)
		{
			auto resourcePtr = resourceStructs[factory.Get()].lock();

			if(!resourcePtr)
			{
				resourcePtr = std::make_shared<T>(factory);
				resourceStructs[factory.Get()] = resourcePtr;
			}

			return resourcePtr;
		}
	};

	class GmpiUiHelper :
		  public gmpi::api::IInputHost
		, public gmpi::api::IDialogHost
		, public gmpi::api::IEditorHost
		, public gmpi::api::IEditorHost2_x
		, public gmpi::api::IDrawingHost
		, public gmpi::api::IParameterSetter_x
		, public synthedit::IEmbeddedFileSupport
	{
		class ModuleView& moduleview;
	public:
		GmpiUiHelper(ModuleView& pmoduleview) : moduleview(pmoduleview) {}

		// IInputHost
		gmpi::ReturnCode setCapture() override;
		gmpi::ReturnCode getCapture(bool& returnValue) override;
		gmpi::ReturnCode releaseCapture() override;
		gmpi::ReturnCode getFocus() override;
		gmpi::ReturnCode releaseFocus() override;
		// IEditorHost
		gmpi::ReturnCode setPin(int32_t pinId, int32_t voice, int32_t size, const void* data) override;
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
		gmpi::ReturnCode setParameter(int32_t parameterHandle, gmpi::Field fieldId, int32_t voice, int32_t size, const void* data) override;

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
			GMPI_QUERYINTERFACE(gmpi::api::IEditorHost2_x);		
			GMPI_QUERYINTERFACE(gmpi::api::IDrawingHost);
			GMPI_QUERYINTERFACE(gmpi::api::IParameterSetter_x);
			GMPI_QUERYINTERFACE(synthedit::IEmbeddedFileSupport);
			return gmpi::ReturnCode::NoSupport;
		}
		GMPI_REFCOUNT;
	};

	class ModuleView : public
		ViewChild
		, public gmpi::IMpUserInterfaceHost2
		, public gmpi::IMpUserInterfaceHost
		, public gmpi_gui::IMpGraphicsHost
	{
	protected:
		Module_Info* moduleInfo;
		GmpiUiHelper uiHelper;

	public:
		static const int SelectionFrameOffset = 1;
		static const int ResizeHandleRadius = 3;

		bool initialised_;
		std::vector<int> inputPinIds;
		bool ignoreMouse;
		int SortOrder = -1;

		ModuleView(const wchar_t* typeId, ViewBase* pParent, int handle);
		ModuleView(Json::Value* context, ViewBase* pParent);

		Module_Info* getModuleType()
		{
			return moduleInfo;
		}

		int32_t GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory) override;
		GmpiDrawing::Factory DrawingFactory();

		std::string name;

		void BuildContainer(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap);
		void BuildContainerCadmium(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap);
		void Build();
		void queryPluginInterfaces(gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown>& object);
		void CreateGraphicsResources();
		bool isMonoDirectional() const
		{
			return !pluginEditor2.isNull();
		}

		// unidirectional modules
		virtual void setDirty() { dirty = true; }
		virtual bool getDirty()
		{
			return dirty;
		}
		void process() override;

		// IMpGraphicsHost support.
		virtual void invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect) override;
		int32_t setCapture() override;
		int32_t getCapture(int32_t& returnValue) override;
		int32_t releaseCapture() override;

		// IMpGraphicsHostBase support.
		int32_t createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override;
		int32_t createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog) override;
		int32_t createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override;
		int32_t createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override;

		void AddConnection(int myPinIndex, ModuleView* otherModule, int otherModulePinIndex)
		{
			connections_.insert({ myPinIndex, connection(myPinIndex, otherModule, otherModulePinIndex) });
			//_RPT2(0, "m:%d AddConnection pin: %d\n", handle, myPinIndex);
		}
		void setTotalPins(int totalPins)
		{
			totalPins_ = totalPins;
		}

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
			if (iid == gmpi::MP_IID_UI_HOST2)
			{
				// important to cast to correct vtable (we have multiple vtables) before reinterpret cast
				*object = reinterpret_cast<IMpUnknown*>(static_cast<IMpUserInterfaceHost2*>(this));
				addRef();
				return gmpi::MP_OK;
			}
			if (iid == gmpi_gui::SE_IID_GRAPHICS_HOST || iid == gmpi_gui::SE_IID_GRAPHICS_HOST_BASE || iid == gmpi::MP_IID_UNKNOWN)
			{
				*object = reinterpret_cast<IMpUnknown*>(static_cast<IMpGraphicsHost*>(this));
				addRef();
				return gmpi::MP_OK;
			}

			if (iid == gmpi::MP_IID_UI_HOST)
			{
				*object = reinterpret_cast<IMpUnknown*>(static_cast<IMpUserInterfaceHost*>(this));
				addRef();
				return gmpi::MP_OK;
			}

			*object = nullptr;
			return gmpi::MP_NOSUPPORT;
		}

		gmpi::IMpUserInterface2* getpluginParameters()
		{
			return pluginParameters;
		}

		void initialize();

		int32_t setPin(ModuleView* fromModule, int32_t fromPinId, int32_t pinId, int32_t voice, int32_t size, const void* data);

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
		bool hitTest(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
		void setHover(bool mouseIsOverMe) override;

		GmpiDrawing::Size OffsetToClient()
		{
			return GmpiDrawing::Size(bounds_.left + pluginGraphicsPos.left, bounds_.top + pluginGraphicsPos.top);
		}

		GmpiDrawing::Point PointToPlugin(GmpiDrawing::Point point)
		{
			return point - OffsetToClient();
		}

		int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
		int32_t onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
		int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
		int32_t onMouseWheel(int32_t flags, int32_t delta, GmpiDrawing_API::MP1_POINT point) override;

		int32_t populateContextMenu(float /*x*/, float /*y*/, gmpi::IMpUnknown* /*contextMenuItemsSink*/) override;
		int32_t onContextMenu(int32_t idx) override;

		std::string getToolTip(GmpiDrawing_API::MP1_POINT point) override;
		void receiveMessageFromAudio(void*) override;

		void OnCableDrag(ConnectorViewBase* dragline, GmpiDrawing::Point dragPoint, float& bestDistance, IViewChild*& bestModule, int& bestPinIndex) override;
		GmpiDrawing::Point getConnectionPoint(CableType cableType, int pinIndex) override;
		std::vector<patchpoint_description>* getPatchPoints();

		void OnMoved(GmpiDrawing::Rect& newRect) override;
		void OnNodesMoved(std::vector<GmpiDrawing::Point>& newNodes) override {}
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
		gmpi::shared_ptr<gmpi::api::IEditor2_x> pluginEditor2;

		// SubView
		gmpi_sdk::mp_shared_ptr<ISubView> subView;

		GmpiDrawing::Rect pluginGraphicsPos;

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

		GMPI_REFCOUNT_NO_DELETE;
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
		static const int plugTextSize = 10;
		static const int plugDiameter = 12;

		sharedGraphicResources_struct(GmpiDrawing::Factory& factory)
		{
			const char* pinFontfamily = "Verdana";
//	        auto factory = g.GetFactory();

			// Left justified text.
			tf_plugs_left = factory.CreateTextFormat(static_cast<float>(plugTextSize), pinFontfamily); // see also measure
			tf_plugs_left.SetLineSpacing(static_cast<float>(plugDiameter), static_cast<float>(plugDiameter - 2)); // squish up text a bit.
			tf_plugs_left.SetTextAlignment(GmpiDrawing::TextAlignment::Leading);

			// Right justified text.
			tf_plugs_right = factory.CreateTextFormat(static_cast<float>(plugTextSize), pinFontfamily);
			tf_plugs_right.SetLineSpacing(static_cast<float>(plugDiameter), static_cast<float>(plugDiameter - 2)); // squish up text a bit.
			tf_plugs_right.SetTextAlignment(GmpiDrawing::TextAlignment::Trailing);

			// Header.
			tf_header = factory.CreateTextFormat(static_cast<float>(plugTextSize) + 2.0f);
			tf_header.SetTextAlignment(GmpiDrawing::TextAlignment::Center);
		}

		~sharedGraphicResources_struct()
		{
			_RPT0(0, "~sharedGraphicResources_struct\n");
		}

		GmpiDrawing::TextFormat tf_plugs_left;
		GmpiDrawing::TextFormat tf_plugs_right;
		GmpiDrawing::TextFormat tf_header;

		std::unordered_map< std::string, gmpi_sdk::mp_shared_ptr<GmpiDrawing_API::IMpPathGeometry> > outlineCache;
	};

	class ModuleViewPanel : public ModuleView
	{
		bool isRackModule_ = {};

	public:
		ModuleViewPanel(const wchar_t* typeId, ViewBase* pParent, int handle);
		ModuleViewPanel(Json::Value* context, class ViewBase* pParent, std::map<int, class ModuleView*>& guiObjectMap);
		virtual void OnRender(GmpiDrawing::Graphics& g) override;
		virtual int32_t measure(GmpiDrawing::Size availableSize, GmpiDrawing::Size* returnDesiredSize) override;
		virtual int32_t arrange(GmpiDrawing::Rect finalRect) override;
		GmpiDrawing::Rect GetClipRect() override;

		bool EndCableDrag(GmpiDrawing_API::MP1_POINT point, ConnectorViewBase* dragline) override;

		bool isVisable() override
		{
			return pluginGraphics || pluginGraphics_GMPI;
		}
		bool hitTest(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
		bool hitTestR(int32_t flags, GmpiDrawing_API::MP1_RECT selectionRect) override;

		bool isShown() override;
		bool isDraggable(bool editEnabled) override;

		std::unique_ptr<SE2::IViewChild> createAdorner(ViewBase* pParent) override;
		bool isRackModule() override
		{
			return isRackModule_;
		}
		void invalidateMeasure() override {}
	};
}
