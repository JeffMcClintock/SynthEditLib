#include <vector>
#include <array>
#include <sstream>
#include <iomanip>

#include "ModuleView.h"
#include "ContainerView.h"
#include "ConnectorView.h"
#include "UgDatabase.h"
#include "legacy_sdk_gui2.h"
#include "modules/shared/xplatform.h"
#include "modules/shared/xplatform_modifier_keys.h"
#include "BundleInfo.h"
#include "ProtectedFile.h"
#include "SubViewPanel.h"
#include "ResizeAdorner.h"
#include "Pile.h"
#include "backends/../GmpiUiDrawing.h" // gmpi-ui version, not SDK3
#include "modules/shared/GraphHelpers.h"
#include "IGuiHost2.h"
#include "modules/se_sdk3_hosting/PresenterCommands.h"
#include "mfc_emulation.h"
#if defined(_DEBUG)
#include "SubViewCadmium.h"
#endif

using namespace gmpi;
using namespace std;
using namespace gmpi::drawing;
using namespace gmpi::drawing;

namespace SE2
{
#if 0 // deprecated
// IInputHost
	ReturnCode GmpiHelper::setCapture() { return (gmpi::ReturnCode) moduleview.setCapture();}
	ReturnCode GmpiHelper::getCapture(bool& returnValue) { int32_t cap{}; moduleview.getCapture(cap); returnValue = cap != 0; return gmpi::ReturnCode::Ok; }
	ReturnCode GmpiHelper::releaseCapture() { return (gmpi::ReturnCode) moduleview.releaseCapture(); }
	//ReturnCode GmpiHelper::getFocus() { return gmpi::ReturnCode::NoSupport; }
	//ReturnCode GmpiHelper::releaseFocus() { return gmpi::ReturnCode::NoSupport; }
	// IEditorHost
	ReturnCode GmpiHelper::setPin(int32_t pinId, int32_t voice, int32_t size, const uint8_t* data) { return (gmpi::ReturnCode) moduleview.pinTransmit(pinId, size, data, voice); }
	int32_t GmpiHelper::getHandle() { return moduleview.handle; }
	// IDrawingHost
	ReturnCode GmpiHelper::getDrawingFactory(gmpi::api::IUnknown** returnFactory) { return moduleview.parent->getDrawingFactory(returnFactory); }
	void GmpiHelper::invalidateRect(const gmpi::drawing::Rect* invalidRect) { moduleview.invalidateRect(reinterpret_cast<const GmpiDrawing_API::MP1_RECT*>(invalidRect)); }
	void GmpiHelper::invalidateMeasure() { moduleview.invalidateMeasure(); }

	float GmpiHelper::getRasterizationScale()
	{
		return 1.0f;
	}
	// IDialogHost
	ReturnCode GmpiHelper::createTextEdit(const gmpi::drawing::Rect* rect, gmpi::api::IUnknown** returnTextEdit)
	{
		(void)rect;
		(void)returnTextEdit;
		return gmpi::ReturnCode::NoSupport;
	}

	ReturnCode GmpiHelper::createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) { return gmpi::ReturnCode::NoSupport; }
	ReturnCode GmpiHelper::createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener)
	{
		(void)r;
		(void)returnKeyListener;
		return gmpi::ReturnCode::NoSupport;
	}
	ReturnCode GmpiHelper::createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) { return gmpi::ReturnCode::NoSupport; }
	ReturnCode GmpiHelper::createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) { return gmpi::ReturnCode::NoSupport; }

	gmpi::ReturnCode GmpiHelper::getParameterHandle(int32_t moduleParameterId, int32_t& returnHandle)
	{
		// get my own parameter handle.
		returnHandle = moduleview.parent->Presenter()->GetPatchManager()->getParameterHandle(moduleview.handle, moduleParameterId);
		return gmpi::ReturnCode::Ok;
	}
	gmpi::ReturnCode GmpiHelper::setParameter(int32_t parameterHandle, gmpi::Field fieldId, int32_t voice, int32_t size, const uint8_t* data)
	{
		auto patchMgr = moduleview.parent->Presenter()->GetPatchManager();

		if (fieldId == gmpi::Field::Value || fieldId == gmpi::Field::Normalized)
		{
			// normalised is sensitive to tiny rounding errors. Causes 'changed' flag to be set even when value is the same. (due to rounding in conversion to real value)

			const float current = (float)patchMgr->getParameterValue(parameterHandle, (gmpi::FieldType)fieldId);

			if (fabsf(current - *(float*)data) < 0.000001f)
			{
				return gmpi::ReturnCode::Ok; // ignore small change.
			}
		}

		// set ANY parameter.
		patchMgr->setParameterValue({ data, static_cast<size_t>(size) }, parameterHandle, (gmpi::FieldType) fieldId);

		if (fieldId == gmpi::Field::Value || fieldId == gmpi::Field::Normalized)
		{
			_RPTN(0, "GmpiHelper::setParameter %d -> %f\n", parameterHandle, *(float*)data);
		}

		return gmpi::ReturnCode::Ok;
	}
	gmpi::ReturnCode GmpiHelper::setDirty()
	{
		moduleview.setDirty();
		return gmpi::ReturnCode::Ok;
	}

	//////////////////////////////////////////////////////// IEmbeddedFileSupport

	gmpi::ReturnCode GmpiHelper::findResourceUri(const char* fileName, /*const char* resourceType,*/ gmpi::api::IString* returnFullUri)
	{
#if 0 // std::filesystem
		std::filesystem::path uri(fileName);
		auto resourceType = uri.extension().generic_string();
		if (!resourceType.empty())
		{
			resourceType = resourceType.substr(1); // remove leading dot.
		}
#else
		const std::wstring fileNameW(Utf8ToWstring(fileName));
		std::wstring r_file, r_path, resourceTypeW;
		decompose_filename(fileNameW, r_file, r_path, resourceTypeW);
		const auto resourceType = WStringToUtf8(resourceTypeW);
#endif

		return (gmpi::ReturnCode) moduleview.FindResourceU(fileName, resourceType.c_str(), (gmpi::IString*)returnFullUri);
	}
	gmpi::ReturnCode GmpiHelper::registerResourceUri(const char* fullUri)
	{
		return (gmpi::ReturnCode) GmpiResourceManager::Instance()->RegisterResourceUri(moduleview.handle, fullUri);
	}
	gmpi::ReturnCode GmpiHelper::openUri(const char* fullUri, gmpi::api::IUnknown** returnStream)
	{
		// TODO
		return gmpi::ReturnCode::NoSupport;
	}
	gmpi::ReturnCode GmpiHelper::clearResourceUris()
	{
		// TODO
		return gmpi::ReturnCode::NoSupport;
	}
#endif

	ReturnCode GmpiHelper::setCapture()
	{
        return moduleview.setCaptureFromHost();
	}

	ReturnCode GmpiHelper::getCapture(bool& returnValue)
	{
     returnValue = moduleview.getCaptureFromHost();
		return gmpi::ReturnCode::Ok;
	}

	ReturnCode GmpiHelper::releaseCapture()
	{
       return moduleview.releaseCaptureFromHost();
	}

	ReturnCode GmpiHelper::setPin(int32_t pinId, int32_t voice, int32_t size, const uint8_t* data)
	{
      return static_cast<gmpi::ReturnCode>(moduleview.pinTransmit(pinId, size, data, voice));
	}

	int32_t GmpiHelper::getHandle()
	{
		return moduleview.handle;
	}

	ReturnCode GmpiHelper::getDrawingFactory(gmpi::api::IUnknown** returnFactory)
	{
		if (!moduleview.parent)
		{
			if (returnFactory)
				*returnFactory = nullptr;
			return gmpi::ReturnCode::Fail;
		}

		return moduleview.parent->getDrawingFactory(returnFactory);
	}

	void GmpiHelper::invalidateRect(const gmpi::drawing::Rect* invalidRect)
	{
     moduleview.InvalidatePluginRect(invalidRect);
	}

	void GmpiHelper::invalidateMeasure()
	{
		if (moduleview.parent)
			moduleview.parent->OnChildResize(&moduleview);
	}

	float GmpiHelper::getRasterizationScale()
	{
		return 1.0f;
	}

	ReturnCode GmpiHelper::createTextEdit(const gmpi::drawing::Rect* localRect, gmpi::api::IUnknown** returnTextEdit)
	{
        const auto adjustedRect = moduleview.MapPluginRectToView(*localRect);

		return moduleview.parent->dialogHost->createTextEdit(&adjustedRect, returnTextEdit);
	}

	ReturnCode GmpiHelper::createPopupMenu(const gmpi::drawing::Rect* localRect, gmpi::api::IUnknown** returnPopupMenu)
	{
        const auto adjustedRect = moduleview.MapPluginRectToView(*localRect);

		return moduleview.parent->dialogHost->createPopupMenu(&adjustedRect, returnPopupMenu);
	}

	ReturnCode GmpiHelper::createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener)
	{
		return moduleview.parent->dialogHost->createKeyListener(r, returnKeyListener);
	}

	ReturnCode GmpiHelper::createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog)
	{
		return moduleview.parent->dialogHost->createFileDialog(dialogType, returnDialog);
	}

	ReturnCode GmpiHelper::createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog)
	{
		return moduleview.parent->dialogHost->createStockDialog(dialogType, returnDialog);
	}

	gmpi::ReturnCode GmpiHelper::getParameterHandle(int32_t moduleParameterId, int32_t& returnHandle)
	{
		returnHandle = moduleview.parent->Presenter()->GetPatchManager()->getParameterHandle(moduleview.handle, moduleParameterId);
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode GmpiHelper::setParameter(int32_t parameterHandle, gmpi::Field fieldId, int32_t voice, int32_t size, const uint8_t* data)
	{
		(void)voice;
		moduleview.parent->Presenter()->GetPatchManager()->setParameterValue({ data, static_cast<size_t>(size) }, parameterHandle, (gmpi::FieldType)fieldId);
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode GmpiHelper::findResourceUri(const char* fileName, gmpi::api::IString* returnFullUri)
	{
		std::string resourceType;
		if (fileName)
		{
			auto p = std::string(fileName).find_last_of('.');
			if (p != std::string::npos)
				resourceType = std::string(fileName).substr(p + 1);
		}

		return static_cast<gmpi::ReturnCode>(GmpiResourceManager::Instance()->FindResourceU(moduleview.handle, moduleview.parent->getSkinName(), fileName, resourceType.c_str(), reinterpret_cast<gmpi::IString*>(returnFullUri)));
	}

	gmpi::ReturnCode GmpiHelper::registerResourceUri(const char* fullUri)
	{
		return static_cast<gmpi::ReturnCode>(GmpiResourceManager::Instance()->RegisterResourceUri(moduleview.handle, fullUri));
	}

	gmpi::ReturnCode GmpiHelper::openUri(const char* fullUri, gmpi::api::IUnknown** returnStream)
	{
		(void)fullUri;
		if (returnStream)
			*returnStream = nullptr;
		return gmpi::ReturnCode::NoSupport;
	}

	gmpi::ReturnCode GmpiHelper::clearResourceUris()
	{
		GmpiResourceManager::Instance()->ClearResourceUris(moduleview.handle);
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode GmpiHelper::setDirty()
	{
		moduleview.setDirty();
		return gmpi::ReturnCode::Ok;
	}

	void Sdk3Helper::OnPatchCablesUpdate(RawView patchCablesRaw) // from PatchCableChangeNotifier
	{
		moduleview.parent->OnPatchCablesUpdate(patchCablesRaw);
	}

	int32_t Sdk3Helper::GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory)
	{
		if (!returnFactory)
			return gmpi::MP_FAIL;

		gmpi::api::IUnknown* gmpiFactory{};
		moduleview.parent->drawingHost->getDrawingFactory(&gmpiFactory);

		// cast drawing host to SDK3
		gmpi_sdk::mp_shared_ptr<gmpi_gui::IMpGraphicsHost> sdk3DrawingHost;
		return (int32_t) gmpiFactory->queryInterface((const gmpi::api::Guid*)&GmpiDrawing_API::SE_IID_FACTORY_MPGUI, (void**) returnFactory);
	}

	void Sdk3Helper::invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect)
	{
		if (invalidRect)
		{
			gmpi::drawing::Rect r{ invalidRect->left, invalidRect->top, invalidRect->right, invalidRect->bottom };
          moduleview.InvalidatePluginRect(&r);
		}
		else
		{
         moduleview.InvalidatePluginRect(nullptr);
		}
	}

	void Sdk3Helper::invalidateMeasure()
	{
		if(!moduleview.initialised_)
			return;

		gmpi::drawing::Size current{ getWidth(moduleview.bounds_), getHeight(moduleview.bounds_) };
		gmpi::drawing::Size desired{};
		moduleview.measure(current, &desired);

		if(current != desired)
		{
			moduleview.parent->Presenter()->DirtyView(); // recreate entire view. async. editor-only.
		}
		else
		{
			// if size remains the same, just arrange and redraw.
			moduleview.arrange(moduleview.bounds_);
			invalidateRect(nullptr);
		}
	}

	int32_t Sdk3Helper::setCapture()
	{
        return static_cast<int32_t>(moduleview.setCaptureFromHost());
	}

	int32_t Sdk3Helper::getCapture(int32_t& returnValue)
	{
     returnValue = moduleview.getCaptureFromHost() ? 1 : 0;
		return gmpi::MP_OK;
	}

	int32_t Sdk3Helper::releaseCapture()
	{
       return static_cast<int32_t>(moduleview.releaseCaptureFromHost());
	}

	int32_t Sdk3Helper::createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog)
	{
		(void)dialogType;
		if (returnFileDialog)
			*returnFileDialog = nullptr;
		return gmpi::MP_UNHANDLED;
	}

	int32_t Sdk3Helper::createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog)
	{
		(void)dialogType;
		if (returnDialog)
			*returnDialog = nullptr;
		return gmpi::MP_UNHANDLED;
	}

	int32_t Sdk3Helper::createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu)
	{
		if(!rect || !returnMenu)
			return gmpi::MP_FAIL;

		*returnMenu = nullptr;

     auto localRect = *reinterpret_cast<gmpi::drawing::Rect*>(rect);
		const auto adjustedRect = moduleview.MapPluginRectToView(localRect);

		gmpi::shared_ptr<gmpi::api::IUnknown> popupMenu;
		const auto result = moduleview.parent->dialogHost->createPopupMenu(&adjustedRect, popupMenu.put());
		if(result != gmpi::ReturnCode::Ok || popupMenu.isNull())
		{
			return gmpi::MP_FAIL;
		}

		const auto queryResult = popupMenu->queryInterface(&gmpi_gui::legacy::IMpPlatformMenu::guid, reinterpret_cast<void**>(returnMenu));
		if(queryResult != gmpi::ReturnCode::Ok || !*returnMenu)
		{
			return gmpi::MP_FAIL;
		}

		return gmpi::MP_OK;
	}

	int32_t Sdk3Helper::createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit)
	{
		// enforce pre-conditions.
		assert(rect);
		assert(returnTextEdit);

		// clear return value first.
		*returnTextEdit = nullptr;

        // adjust coordinates to parent using the module's full transform.
     auto localRect = *reinterpret_cast<gmpi::drawing::Rect*>(rect);
		const auto adjustedRect = moduleview.MapPluginRectToView(localRect);

		// create a GMPI-UI widget.
		gmpi::shared_ptr< gmpi::api::IUnknown> retWidget;
		auto retValue = moduleview.parent->dialogHost->createTextEdit(&adjustedRect, retWidget.put());

		if(retValue != gmpi::ReturnCode::Ok || retWidget.isNull())
			return gmpi::MP_FAIL;

		// cast GMPI-UI widget to it's legacy equivalent.
		return (int32_t) retWidget->queryInterface(&gmpi_gui::legacy::IMpPlatformText::guid, (void**)returnTextEdit);
	}

	int32_t Sdk3Helper::pinTransmit(int32_t pinId, int32_t size, const void* data, int32_t voice)
	{
      return moduleview.pinTransmit(pinId, size, data, voice);
	}

	int32_t Sdk3Helper::sendMessageToAudio(int32_t id, int32_t size, const void* messageData)
	{
		return moduleview.Presenter()->GetPatchManager()->sendSdkMessageToAudio(moduleview.handle, id, size, messageData);
	}

	int32_t Sdk3Helper::getHandle(int32_t& returnValue)
	{
		returnValue = moduleview.handle;
		return gmpi::MP_OK;
	}

	int32_t ModuleView::pinTransmit(int32_t pinId, int32_t size, const void* data, int32_t voice)
	{
		auto it = connections_.find(pinId);
		while (it != connections_.end() && it->first == pinId)
		{
			auto& connection = it->second;
			connection.otherModule_->setPin(this, pinId, connection.otherModulePinIndex_, voice, size, data);
			++it;
		}

		if (!initialised_)
		{
			alreadySentDataPins_.push_back(pinId);
		}

		if (recursionStopper_ < 10)
		{
			const bool isInputPin = std::find(inputPinIds.begin(), inputPinIds.end(), pinId) != inputPinIds.end();
			if (isInputPin)
			{
				++recursionStopper_;

				if (pluginParameters_GMPI)
				{
					pluginParameters_GMPI->setPin(pinId, voice, size, static_cast<const uint8_t*>(data));
					pluginParameters_GMPI->notifyPin(pinId, voice);
				}
				else if (pluginParameters)
				{
					pluginParameters->setPin(pinId, voice, size, data);
					if (pluginParameters2B)
					{
						pluginParameters2B->notifyPin(pinId, voice);
					}
				}
				else if (pluginParametersLegacy)
				{
					pluginParametersLegacy->setPin(pinId, voice, size, const_cast<void*>(data));
					pluginParametersLegacy->notifyPin(pinId, voice);
				}

				--recursionStopper_;
			}
		}

		return gmpi::MP_OK;
	}

	int32_t Sdk3Helper::createPinIterator(gmpi::IMpPinIterator** returnIterator)
	{
		if (returnIterator)
			*returnIterator = nullptr;
		return gmpi::MP_UNHANDLED;
	}

	int32_t Sdk3Helper::ClearResourceUris()
	{
		GmpiResourceManager::Instance()->ClearResourceUris(moduleview.handle);
		return gmpi::MP_OK;
	}

	int32_t Sdk3Helper::RegisterResourceUri(const char* resourceName, const char* resourceType, gmpi::IString* returnString)
	{
		return GmpiResourceManager::Instance()->RegisterResourceUri(moduleview.handle, moduleview.parent->getSkinName(), resourceName, resourceType, returnString);
	}

	int32_t Sdk3Helper::FindResourceU(const char* resourceName, const char* resourceType, gmpi::IString* returnString)
	{
		return GmpiResourceManager::Instance()->FindResourceU(moduleview.handle, moduleview.parent->getSkinName(), resourceName, resourceType, returnString);
	}

	int32_t Sdk3Helper::LoadPresetFile_DEPRECATED(const char* presetFilePath)
	{
		(void)presetFilePath;
		return gmpi::MP_UNHANDLED;
	}

	int32_t Sdk3Helper::OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream)
	{
		return GmpiResourceManager::Instance()->OpenUri(fullUri, returnStream);
	}

	int32_t Sdk3Helper::pinTransmit(int32_t pinId, int32_t size, void* data, int32_t voice)
	{
		return pinTransmit(pinId, size, static_cast<const void*>(data), voice);
	}

	int32_t Sdk3Helper::sendMessageToAudio(int32_t id, int32_t size, void* messageData)
	{
		return sendMessageToAudio(id, size, static_cast<const void*>(messageData));
	}

	int32_t Sdk3Helper::setIdleTimer(int32_t active)
	{
		(void)active;
		return gmpi::MP_UNHANDLED;
	}

	int32_t Sdk3Helper::getHostId(int32_t maxChars, wchar_t* returnString)
	{
		if (!returnString || maxChars <= 0)
			return gmpi::MP_FAIL;

		const wchar_t* hostName = L"SynthEdit";
#if defined(_MSC_VER)
		wcscpy_s(returnString, maxChars, hostName);
#else
		wcscpy(returnString, hostName);
#endif
		return gmpi::MP_OK;
	}

	int32_t Sdk3Helper::getHostVersion(int32_t& returnValue)
	{
		returnValue = 1000;
		return gmpi::MP_OK;
	}

	int32_t Sdk3Helper::resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename)
	{
		return moduleview.Presenter()->GetPatchManager()->resolveFilename(shortFilename, maxChars, returnFullFilename);
	}

	int32_t Sdk3Helper::addContextMenuItem(wchar_t* menuText, int32_t index, int32_t flags)
	{
		(void)menuText;
		(void)index;
		(void)flags;
		return gmpi::MP_UNHANDLED;
	}

	int32_t Sdk3Helper::getPinCount(int32_t& returnCount)
	{
		if (moduleview.totalPins_ > -1)
		{
			returnCount = moduleview.totalPins_;
			return gmpi::MP_OK;
		}

		return gmpi::MP_UNHANDLED;
	}

	int32_t Sdk3Helper::openProtectedFile(const wchar_t* shortFilename, gmpi::IProtectedFile** file)
	{
		(void)shortFilename;
		if (file)
			*file = nullptr;
		return gmpi::MP_UNHANDLED;
	}

	int32_t Sdk3Helper::queryInterface(const gmpi::MpGuid& iid, void** object)
	{
		if (!object)
			return gmpi::MP_FAIL;

		*object = nullptr;

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

		if (iid == gmpi_gui::SE_IID_GRAPHICS_HOST || iid == gmpi_gui::SE_IID_GRAPHICS_HOST_BASE || iid == gmpi::MP_IID_UNKNOWN)
		{
			*object = reinterpret_cast<IMpUnknown*>(static_cast<IMpGraphicsHost*>(this));
			addRef();
			return gmpi::MP_OK;
		}

		return gmpi::MP_NOSUPPORT;
	}

	////////////////////////////////////////////////////////

	ModuleView::ModuleView(const wchar_t* typeId, ViewBase* pParent, int handle) : ViewChild(pParent, handle)
//		, uiHelper(*this)
		, recursionStopper_(0)
		, initialised_(false)
		, ignoreMouse(false)
	{
		moduleInfo = CModuleFactory::Instance()->GetById(typeId);
	}

	ModuleView::ModuleView(Json::Value* context, ViewBase* pParent) : ViewChild(context, pParent)
//		, uiHelper(*this)
		, recursionStopper_(0)
		, initialised_(false)
		, ignoreMouse(false)
	{
		auto& module_element = *context;
		if (pParent->getViewType() == CF_PANEL_VIEW)
		{
			bounds_.left = module_element["pl"].asFloat();
			bounds_.top = module_element["pt"].asFloat();
			bounds_.right = module_element["pr"].asFloat();
			bounds_.bottom = module_element["pb"].asFloat();
		}
		else
		{
			bounds_.left = module_element["sl"].asFloat();
			bounds_.top = module_element["st"].asFloat();
			bounds_.right = module_element["sr"].asFloat();
			bounds_.bottom = module_element["sb"].asFloat();
		}
		setSelected(module_element["selected"].asBool() );

		auto typeName = module_element["type"].asString();
		const auto typeId = JmUnicodeConversions::Utf8ToWstring(typeName);
		moduleInfo = CModuleFactory::Instance()->GetById(typeId);
        assert( moduleInfo != nullptr );
	}

	ModuleViewPanel::ModuleViewPanel(const wchar_t* typeId, ViewBase* pParent, int handle) : ModuleView(typeId, pParent, handle)
	{
		assert(moduleInfo != nullptr && moduleInfo->UniqueId() != L"Container");
		Build();
	}

	ModuleViewPanel::ModuleViewPanel(Json::Value* context, class ViewBase* pParent, std::map<int, class ModuleView*>& guiObjectMap) : ModuleView(context, pParent)
	{
		if (!moduleInfo) // missing module
			return;

		auto& module_element = *context;

		ignoreMouse = !module_element["ignoremouse"].empty();

		if(moduleInfo->UniqueId() == L"Container")
		{
			isRackModule_ = !module_element["isRackModule"].empty();

			const auto isCadmiumView = !module_element["isCadmiumView"].empty();
			if (isCadmiumView)
			{
				BuildContainerCadmium(context, guiObjectMap);
			}
			else
			{
				BuildContainer(context, guiObjectMap);
			}
		}
		else
		{
			Build();
		}
	}

	void ModuleView::BuildContainer(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap)
	{
		assert(moduleInfo->UniqueId() == L"Container");

		auto subView = new SubView(this, parent->getViewType());

		gmpi::shared_ptr<gmpi::api::IUnknown> object;
		object.attach(static_cast<ISubView*>(subView));
		assert(object != nullptr);
#if 0
		{
			auto r = object->queryInterface(gmpi::MP_IID_GUI_PLUGIN2, pluginParameters.asIMpUnknownPtr());
			r = object->queryInterface(gmpi::MP_IID_GUI_PLUGIN2B, pluginParameters2B.asIMpUnknownPtr());
			r = object->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI3, pluginGraphics3.asIMpUnknownPtr());
			r = object->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI4, pluginGraphics4.asIMpUnknownPtr());
			r = object->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI2, pluginGraphics2.asIMpUnknownPtr());
			r = object->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI, pluginGraphics.asIMpUnknownPtr());

			pluginParameters->setHost(static_cast<gmpi::IMpUserInterfaceHost2*>(this));
		}
#endif
		queryPluginInterfaces(object);

		auto subPresenter = Presenter()->CreateSubPresenter(handle);
		subView->Init(subPresenter);
		
		subView->setHost(static_cast<gmpi::api::IEditorHost*>(gmpiHelper.get()));

		subView->BuildModules(context, guiObjectMap);

		if (Presenter()->GetPatchManager() != subPresenter->GetPatchManager())
		{
			subView->BuildPatchCableNotifier(guiObjectMap);
		}
	}

	void ModuleView::BuildContainerCadmium(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap)
	{
#if 0 // for now defined(_DEBUG)
		assert(moduleInfo->UniqueId() == L"Container");

		gmpi_sdk::mp_shared_ptr<gmpi::api::IUnknown> object;
		object.Attach(static_cast<gmpi::IMpUserInterface2B*>(new SubViewCadmium(parent->getViewType())));
		assert(object != nullptr);
		{
			auto r = object->queryInterface(gmpi::MP_IID_GUI_PLUGIN2, pluginParameters.asIMpUnknownPtr());
			r = object->queryInterface(gmpi::MP_IID_GUI_PLUGIN2B, pluginParameters2B.asIMpUnknownPtr());
			r = object->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI, pluginGraphics.asIMpUnknownPtr());
			r = object->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI2, pluginGraphics2.asIMpUnknownPtr());
			r = object->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI3, pluginGraphics3.asIMpUnknownPtr());
			r = object->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI4, pluginGraphics4.asIMpUnknownPtr());

			pluginParameters->setHost(static_cast<gmpi::IMpUserInterfaceHost2*>(this));
		}

		auto subView = dynamic_cast<SubViewCadmium*>(pluginGraphics.get());
		auto subPresenter = Presenter()->CreateSubPresenter(handle);
		subView->Init(subPresenter);

#ifdef _DEBUG
		{
			Json::StyledWriter writer;
			auto factoryXml = writer.write(*context);
			auto s = factoryXml;
		}
#endif

		subView->BuildView(context); // , guiObjectMap);

		if (Presenter()->GetPatchManager() != subPresenter->GetPatchManager())
		{
			subView->BuildPatchCableNotifier(guiObjectMap);
		}
#endif
	}

	void ModuleView::Build()
	{
		auto& mi = moduleInfo;

		gmpi::shared_ptr<gmpi::api::IUnknown> object;
		object.attach(reinterpret_cast<gmpi::api::IUnknown*>(mi->Build(MP_SUB_TYPE_GUI2, true)));

		if (!object && mi->getWindowType() == MP_WINDOW_TYPE_NONE) // can't support legacy graphics, but can support invisible legacy sub-controls.
		{
			object.attach(reinterpret_cast<gmpi::api::IUnknown*>(mi->Build(MP_SUB_TYPE_GUI, true)));
		}

		if (!object)
		{
#if defined( SE_EDIT_SUPPORT ) && defined( _DEBUG)
			if (!mi->m_dsp_registered) // try to avoid false positives (DSP-only modules)
			{
				_RPTN(0, "FAILED TO LOAD: %S\n", mi->UniqueId().c_str());
				mi->load_failed_gui = true;
			}
#endif

			return;
		}

		queryPluginInterfaces(object);
	}

	void ModuleView::queryPluginInterfaces(gmpi::shared_ptr<gmpi::api::IUnknown>& object)
	{
		// GMPI-UI client
		{
			subView = object.as<ISubView>();
			pluginEditor2 = object.as<gmpi::api::IEditor2>();
			pluginInput_GMPI = object.as<gmpi::api::IInputClient>();
			pluginParameters_GMPI = object.as<gmpi::api::IEditor>();
			pluginGraphics_GMPI = object.as<gmpi::api::IDrawingClient>();
			pluginDrawingLayer_GMPI = object.as<gmpi::api::IDrawingLayer>();

			if(pluginParameters_GMPI || pluginGraphics_GMPI || pluginEditor2)
				gmpiHelper = std::make_unique<GmpiHelper>(*this);

			if(pluginParameters_GMPI)
				pluginParameters_GMPI->setHost(static_cast<gmpi::api::IEditorHost*>(gmpiHelper.get()));
			//else if(subView)
			//	subView->setHost(static_cast<gmpi::api::IEditorHost*>(gmpiHelper.get()));

			if(pluginGraphics_GMPI)
				pluginGraphics_GMPI->open(static_cast<gmpi::api::IDrawingHost*>(gmpiHelper.get()));
		}

		// SDK3 client
		if(!pluginParameters_GMPI && !pluginGraphics_GMPI)
		{
			auto r = object->queryInterface(reinterpret_cast<const gmpi::api::Guid*>(&gmpi::MP_IID_GUI_PLUGIN2), pluginParameters.asIMpUnknownPtr());
			r = object->queryInterface(reinterpret_cast<const gmpi::api::Guid*>(&gmpi::MP_IID_GUI_PLUGIN2B), pluginParameters2B.asIMpUnknownPtr());
			r = object->queryInterface(reinterpret_cast<const gmpi::api::Guid*>(&gmpi::MP_IID_GUI_PLUGIN), pluginParametersLegacy.asIMpUnknownPtr());
			r = object->queryInterface(reinterpret_cast<const gmpi::api::Guid*>(&gmpi_gui_api::SE_IID_GRAPHICS_MPGUI), pluginGraphics.asIMpUnknownPtr());
			r = object->queryInterface(reinterpret_cast<const gmpi::api::Guid*>(&gmpi_gui_api::SE_IID_GRAPHICS_MPGUI2), pluginGraphics2.asIMpUnknownPtr());
			r = object->queryInterface(reinterpret_cast<const gmpi::api::Guid*>(&gmpi_gui_api::SE_IID_GRAPHICS_MPGUI3), pluginGraphics3.asIMpUnknownPtr());
			r = object->queryInterface(reinterpret_cast<const gmpi::api::Guid*>(&gmpi_gui_api::SE_IID_GRAPHICS_MPGUI4), pluginGraphics4.asIMpUnknownPtr());

			if(pluginGraphics || pluginParameters || pluginParametersLegacy)
				sdk3Helper = std::make_unique<Sdk3Helper>(*this);

if(pluginGraphics)
	object->queryInterface(&ISubView::guid, subView.put_void());

			if(!pluginParameters.isNull())
			{
				pluginParameters->setHost(static_cast<gmpi::IMpUserInterfaceHost2*>(sdk3Helper.get()));
			}
			else
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpLegacyInitialization> legacyInitMethod;
				r = object->queryInterface(reinterpret_cast<const gmpi::api::Guid*>(&gmpi::MP_IID_LEGACY_INITIALIZATION), legacyInitMethod.asIMpUnknownPtr());
				if(!legacyInitMethod.isNull())
				{
					legacyInitMethod->setHost(static_cast<IMpUserInterfaceHost*>(sdk3Helper.get()));
				}
				else
				{
					// last gasp
					// CAN'T/SHOULDN"T DYNAMIC CAST INTO DLL!!!! (but we have to support old 3rd-party modules)
					auto oldSchool = dynamic_cast<IoldSchoolInitialisation*>(object.get());
					if(oldSchool)
						oldSchool->setHost(static_cast<IMpUserInterfaceHost*>(sdk3Helper.get()));
				}
			}
		}
	}

	void ModuleView::initialize()
	{
		if (pluginParameters_GMPI)
			pluginParameters_GMPI->initialize();
		else if (pluginParameters)
			pluginParameters->initialize();
		else if (pluginParametersLegacy)
			pluginParametersLegacy->initialize();

		initialised_ = true;
		// outputValues_ is only needed while connecting modules. could be centralised further to view.
		std::vector<int>().swap(alreadySentDataPins_);
	}

	void ModuleView::CreateGraphicsResources()
	{
	}

	gmpi::drawing::Matrix3x2 ModuleView::GetTransformToTopView() const
	{
		auto transform = gmpi::drawing::makeTranslation(bounds_.left, bounds_.top);

		if (parent)
			transform = transform * parent->GetTransformToTopView();

		return transform;
	}

	gmpi::drawing::Rect ModuleView::MapPluginRectToView(const gmpi::drawing::Rect& localRect) const
	{
		auto transform = gmpi::drawing::makeTranslation(pluginGraphicsPos.left, pluginGraphicsPos.top) * GetTransformToTopView();
		return gmpi::drawing::transformRect(transform, localRect);
	}

	void ModuleView::InvalidatePluginRect(const gmpi::drawing::Rect* invalidRect)
	{
		if (!parent)
			return;

		if (invalidRect)
		{
			auto r = offsetRect(*invalidRect, { bounds_.left + pluginGraphicsPos.left, bounds_.top + pluginGraphicsPos.top });
			parent->ChildInvalidateRect(r);
		}
		else
		{
			parent->ChildInvalidateRect(bounds_);
		}
	}

	gmpi::ReturnCode ModuleView::setCaptureFromHost()
	{
		mouseCaptured = true;
		return parent ? static_cast<gmpi::ReturnCode>(parent->setCapture(this)) : gmpi::ReturnCode::Fail;
	}

	gmpi::ReturnCode ModuleView::releaseCaptureFromHost()
	{
		mouseCaptured = false;
		return parent ? static_cast<gmpi::ReturnCode>(parent->releaseCapture()) : gmpi::ReturnCode::Fail;
	}

	bool ModuleView::getCaptureFromHost() const
	{
		return mouseCaptured;
	}
	
	gmpi::drawing::PathGeometry ModuleView::getOutline(gmpi::drawing::Factory drawingFactory)
	{
		return {};
	}

	gmpi::ReturnCode ModuleView::onPointerDown(gmpi::drawing::Point point, int32_t flags)
	{
		if(ignoreMouse) // background image.
			return gmpi::ReturnCode::Unhandled;

		// pluginGraphics2 supports hit-testing, else need to call onPointerDown() to determin hit on client.
		if(pluginGraphics_GMPI || pluginGraphics2 || parent->getViewType() == CF_STRUCTURE_VIEW) // Since Structure view is "behind" client, it always gets selected.
			Presenter()->ObjectClicked(handle, flags);

		auto moduleLocal = point;
		moduleLocal.x -= bounds_.left;
		moduleLocal.y -= bounds_.top;

		bool clientHit = false;

		// Mouse over client area?
		if((pluginGraphics_GMPI || pluginGraphics) && pointInRect(moduleLocal, pluginGraphicsPos))
		{
			auto local = PointToPlugin(point);

			// Patch-Points. Initiate drag on left-click.
			if((flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0)
			{
				auto patchPoints = getPatchPoints();
				if(patchPoints != nullptr)
				{
					for(auto& p : *patchPoints)
					{
						float distanceSquared = (local.x - p.x) * (local.x - p.x) + (local.y - p.y) * (local.y - p.y);
						if(distanceSquared <= p.radius * p.radius)
						{
							gmpi::drawing::Point dragStartPoint = gmpi::drawing::Point(static_cast<float>(p.x), static_cast<float>(p.y)) + gmpi::drawing::Size(bounds_.left + pluginGraphicsPos.left, bounds_.top + pluginGraphicsPos.top);
							parent->StartCableDrag(this, p.dspPin, dragStartPoint, point);
							return gmpi::ReturnCode::Handled;
						}
					}
				}
			}

			auto res = gmpi::ReturnCode::Unhandled;

			if(pluginGraphics_GMPI || pluginGraphics2) // Client supports proper hit testing.
			{
				// In Panel-view, we can assume mouse already hit-tested against client. On Structure-view it could be a click on client OR on pins.
				clientHit = parent->getViewType() == CF_PANEL_VIEW;

				if(!clientHit)
				{
					if(subView)
						clientHit = subView->hitTest(flags, &local);
					else
					{
						if(pluginInput_GMPI)
							clientHit = gmpi::ReturnCode::Ok == pluginInput_GMPI->hitTest(local, flags);
						else if(pluginGraphics2)
							clientHit = gmpi::ReturnCode::Ok == (gmpi::ReturnCode) pluginGraphics2->hitTest(*reinterpret_cast<GmpiDrawing_API::MP1_POINT*>(&local));
					}
				}

				// In Panel-view, we can assume mouse already hit-tested against client. On Structure-view it could be a click on client OR on pins.
				if(clientHit)
				{
					if(pluginInput_GMPI)
						res = pluginInput_GMPI->onPointerDown(local, flags);
					if(pluginGraphics)
						res = (gmpi::ReturnCode) pluginGraphics->onPointerDown(flags, *reinterpret_cast<GmpiDrawing_API::MP1_POINT*>(&local));// older modules indicate hit via return value.
				}
			}
			else
			{
				// Old system: Hit-testing inferred from onPointerDown() return value;
				res = (gmpi::ReturnCode) pluginGraphics->onPointerDown(flags, *reinterpret_cast<GmpiDrawing_API::MP1_POINT*>(&local)); // older modules indicate hit via return value.

				clientHit = (res == gmpi::ReturnCode::Ok || res == gmpi::ReturnCode::Handled);

				if(clientHit && parent->getViewType() == CF_PANEL_VIEW)
					Presenter()->ObjectClicked(handle, flags); //gmpi::modifier_keys::getHeldKeys());
			}

			if(gmpi::ReturnCode::Handled == res) // Client indicates no further processing needed.
				return gmpi::ReturnCode::Handled;
		}

		// don't handle right-clicks, otherwise context menu is not shown.
		return clientHit && ((flags & gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON) == 0) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ModuleView::onPointerMove(gmpi::drawing::Point point, int32_t flags)
	{
		const auto local = PointToPlugin(point);

		if(pluginInput_GMPI)
			return pluginInput_GMPI->onPointerMove(local, flags);
		else if(pluginGraphics)
			return (gmpi::ReturnCode) pluginGraphics->onPointerMove(flags, *reinterpret_cast<const GmpiDrawing_API::MP1_POINT*>(&local));

		return gmpi::ReturnCode::Unhandled;
	}
	gmpi::ReturnCode ModuleView::onPointerUp(gmpi::drawing::Point point, int32_t flags)
	{
		const auto local = PointToPlugin(point);

		if(pluginInput_GMPI)
            return pluginInput_GMPI->onPointerUp(local, flags);
		else if(pluginGraphics)
            return (gmpi::ReturnCode) pluginGraphics->onPointerUp(flags, *reinterpret_cast<const GmpiDrawing_API::MP1_POINT*>(&local));

		return gmpi::ReturnCode::Unhandled;
	}
	gmpi::ReturnCode ModuleView::onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta)
	{
		const auto local = PointToPlugin(point);

		if(pluginInput_GMPI)
            return pluginInput_GMPI->onMouseWheel(local, flags, delta);
		else if(pluginGraphics3)
            return (gmpi::ReturnCode) pluginGraphics3->onMouseWheel(flags, delta, *reinterpret_cast<const GmpiDrawing_API::MP1_POINT*>(&local));

		return gmpi::ReturnCode::Unhandled;
	}

	void ModuleViewPanel::measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize)
	{
		if (pluginGraphics_GMPI)
		{
			gmpi::drawing::Size remainingSizeU{ availableSize.width, availableSize.height };
			gmpi::drawing::Size desiredSizeU{};
			pluginGraphics_GMPI->measure(&remainingSizeU, &desiredSizeU);

			returnDesiredSize->width = static_cast<float>(desiredSizeU.width);
			returnDesiredSize->height = static_cast<float>(desiredSizeU.height);
		}
		else if (pluginGraphics)
		{
			pluginGraphics->measure(*reinterpret_cast<GmpiDrawing_API::MP1_SIZE*>(&availableSize), reinterpret_cast<GmpiDrawing_API::MP1_SIZE*>(returnDesiredSize));
		}
		else
		{
			*returnDesiredSize = availableSize;
		}
	}

	gmpi::drawing::Rect ModuleViewPanel::getClipArea()
	{
		auto clipArea = ModuleView::getClipArea();

		if(isHovered_ && BundleInfo::instance()->isEditor)
			clipArea = unionRect(clipArea, inflateRect(bounds_, 2.f));

		if (pluginGraphics_GMPI)
		{
			drawing::Rect clientClipArea_gmpi{};
			pluginGraphics_GMPI->getClipArea(&clientClipArea_gmpi);

			gmpi::drawing::Rect clientClipArea{ static_cast<float>(clientClipArea_gmpi.left), static_cast<float>(clientClipArea_gmpi.top), static_cast<float>(clientClipArea_gmpi.right), static_cast<float>(clientClipArea_gmpi.bottom) };
			clientClipArea = offsetRect(clientClipArea, { bounds_.left + pluginGraphicsPos.left, bounds_.top + pluginGraphicsPos.top });
			clipArea = unionRect(clipArea, clientClipArea);
		}
		else if (pluginGraphics4)
		{
			GmpiDrawing_API::MP1_RECT clientClipAreaLegacy{};
			pluginGraphics4->getClipArea(&clientClipAreaLegacy);
			gmpi::drawing::Rect clientClipArea{ clientClipAreaLegacy.left, clientClipAreaLegacy.top, clientClipAreaLegacy.right, clientClipAreaLegacy.bottom };
			clientClipArea = offsetRect(clientClipArea, { bounds_.left + pluginGraphicsPos.left, bounds_.top + pluginGraphicsPos.top });
			clipArea = unionRect(clipArea, clientClipArea);
		}

		return clipArea;
	}

	void ModuleViewPanel::arrange(gmpi::drawing::Rect finalRect)
	{
		bounds_ = finalRect; // TODO put in base class.

		if (pluginGraphics_GMPI)
		{
			pluginGraphicsPos = gmpi::drawing::Rect(0, 0, finalRect.right - finalRect.left, finalRect.bottom - finalRect.top);
			drawing::Rect gmpiRect{ 0, 0, pluginGraphicsPos.right, pluginGraphicsPos.bottom };
			pluginGraphics_GMPI->arrange(&gmpiRect);
		}
		else if (pluginGraphics)
		{
			pluginGraphicsPos = gmpi::drawing::Rect(0, 0, finalRect.right - finalRect.left, finalRect.bottom - finalRect.top);
			pluginGraphics->arrange(*reinterpret_cast<GmpiDrawing_API::MP1_RECT*>(&pluginGraphicsPos));
		}
	}
#if 0 // deprecated

	int32_t ModuleView::GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory)
	{
		*returnFactory = parent->GetDrawingFactory();
		return gmpi::MP_OK;
	}
#endif

	void ModuleViewPanel::render(Graphics& g)
	{
		if (pluginGraphics_GMPI)
		{
			auto gmpiContext = AccessPtr::get(g);
			assert(gmpiContext);

			pluginGraphics_GMPI->render(gmpiContext);

			// todo, second pass for all these at once.
			if (pluginDrawingLayer_GMPI)
				pluginDrawingLayer_GMPI->renderLayer(gmpiContext, 1);
		}

		else if(pluginGraphics)
		{

#if 0 // debug layout and clip rects
			g.fillRectangle(getClipArea(), g.createSolidColorBrush(Color::FromArgb(0x200000ff)));
			g.fillRectangle(getLayoutRect(), g.createSolidColorBrush(Color::FromArgb(0x2000ff00)));
#endif
			/*
					// Transform to module-relative.
					const auto originalTransform = g.getTransform();
					auto adjustedTransform = Matrix3x2::Translation(bounds_.left , bounds_.top) * originalTransform;
					g.setTransform(adjustedTransform);
			*/
			// Render.
			pluginGraphics->OnRender(reinterpret_cast<GmpiDrawing_API::IMpDeviceContext*>(AccessPtr::get(g)));

#if 0 //def _DEBUG
			// Alignment marks.
			{
				auto brsh = g.createSolidColorBrush(Colors::Red);
				g.fillRectangle(Rect(0, 0, 1, 1), brsh);
				g.fillRectangle(Rect(64, 64, 65, 65), brsh);
			}
#endif
			// Transform back.
	//		g.setTransform(originalTransform);
		}

		if(isHovered_ && !getSelected())
		{
			auto brush = g.createSolidColorBrush(gmpi::drawing::Colors::DodgerBlue);
			gmpi::drawing::Rect r(0, 0, getWidth(bounds_), getHeight(bounds_));
			g.drawRoundedRectangle({ r, 2.f, 2.f }, brush, 2.f);
		}
	}

	gmpi::drawing::Point ModuleView::getConnectionPoint(CableType cableType, int pinIndex)
	{
		assert(cableType == CableType::PatchCable);

		for (auto& patchpoint : getModuleType()->patchPoints)
		{
			if (patchpoint.dspPin == pinIndex)
			{
				auto modulePosition = getLayoutRect();
//				return gmpi::drawing::Point(patchpoint.x + modulePosition.left, patchpoint.y + modulePosition.top);
				return gmpi::drawing::Point(patchpoint.x + modulePosition.left + pluginGraphicsPos.left, patchpoint.y + modulePosition.top + pluginGraphicsPos.top);
				break;
			}
		}

		return gmpi::drawing::Point();
	}

#if 0 // deprecated
	int32_t ModuleView::createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit)
	{
		(void)rect;
		(void)returnTextEdit;
		return gmpi::MP_NOSUPPORT;
	}

	int32_t ModuleView::createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu)
	{
		(void)rect;
		(void)returnMenu;
		return gmpi::MP_NOSUPPORT;
	}

	int32_t ModuleView::createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog)
	{
		(void)dialogType;
		(void)returnFileDialog;
		return gmpi::MP_NOSUPPORT;
	}
	int32_t ModuleView::createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnFileDialog)
	{
		(void)dialogType;
		(void)returnFileDialog;
		return gmpi::MP_NOSUPPORT;
	}

	void ModuleView::invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect)
	{ 
		if (invalidRect)
		{
			gmpi::drawing::Rect r{ invalidRect->left, invalidRect->top, invalidRect->right, invalidRect->bottom };
			r = offsetRect(r, { bounds_.left + pluginGraphicsPos.left, bounds_.top + pluginGraphicsPos.top });
			parent->ChildInvalidateRect(r);
		}
		else
			parent->ChildInvalidateRect(bounds_);
	}

	int32_t ModuleView::setCapture()
	{
		mouseCaptured = true;
		return parent->setCapture(this); // getGuiHost()->setCapture();
	}

	int32_t ModuleView::getCapture(int32_t& returnValue)
	{
		returnValue = mouseCaptured;
		//	returnValue = WpfHost->IsMouseCaptured;
		return gmpi::MP_OK;
	}

	int32_t ModuleView::releaseCapture()
	{
		mouseCaptured = false;
		return parent->releaseCapture();
	}
#endif

	void ModuleView::OnMoved(gmpi::drawing::Rect& newRect)
	{
		gmpi::drawing::Rect invalidRect(getClipArea());

		// measure/arrange if nesc.
		gmpi::drawing::Size origSize(getWidth(bounds_), getHeight(bounds_));
		gmpi::drawing::Size newSize(getWidth(newRect), getHeight(newRect));
		if (newSize != origSize)
		{
			// Note, due to font width diferences, this may result in different size/layout than original GDI graphics. e..g knobs shifting.
			gmpi::drawing::Size desired(newSize);
			measure(newSize, &desired);
			arrange(gmpi::drawing::Rect(newRect.left, newRect.top, newRect.left + desired.width, newRect.top + desired.height));
		}
		else
		{
			arrange(newRect);
		}

		invalidRect = unionRect(invalidRect, getClipArea());

		parent->ChildInvalidateRect(invalidRect);

		// update any parent subview
		parent->OnChildMoved();
	}
#if 0 // TODO

	int32_t ModuleView::getHandle(int32_t& returnValue)
	{
		returnValue = handle;
		return gmpi::MP_OK;
	}
	
	int32_t ModuleView::sendMessageToAudio(int32_t id, int32_t size, const void* messageData)
	{
//		return parent->getGuiHost2()->sendSdkMessageToAudio( handle,  id,  size,  messageData);
		return Presenter()->GetPatchManager()->sendSdkMessageToAudio(handle, id, size, messageData);
	}

	int32_t ModuleView::RegisterResourceUri(const char* resourceName, const char* resourceType, gmpi::IString* returnString)
	{
		return GmpiResourceManager::Instance()->RegisterResourceUri(handle, parent->getSkinName(), resourceName, resourceType, returnString);
	}

	int32_t ModuleView::FindResourceU(const char* resourceName, const char* resourceType, gmpi::IString* returnString)
	{
		return GmpiResourceManager::Instance()->FindResourceU(handle, parent->getSkinName(), resourceName, resourceType, returnString);
	}

	int32_t ModuleView::LoadPresetFile_DEPRECATED(const char* presetFilePath)
	{
//		Presenter()->LoadPresetFile(presetFilePath);
		return gmpi::MP_OK;
	}
#endif

	// GUI pins.
	int32_t ModuleView::setPin(ModuleView* fromModule, int32_t fromPinId, int32_t pinId, int32_t voice, int32_t size, const void* data)
	{
		if (recursionStopper_ < 10)
		{
			++recursionStopper_;
#if 0
			auto c = (const char*)data;
			_RPT3(_CRT_WARN, "ModuleView[%S]::setPin: pin=%d, voice=%d val=", moduleInfo->GetName().c_str(), pinId, voice);
			for (int i = 0; i < size; ++i)
			{
				_RPT1(_CRT_WARN, "%02X", 0xFF & (int)c[i]);
			}
			_RPT0(_CRT_WARN, "\n");
#endif

			// Notify my module.
			if (pluginParameters_GMPI)
			{
				pluginParameters_GMPI->setPin(pinId, voice, size, (const uint8_t*) data);
				if (isMonoDirectional())
				{
					// monodirection method, mark as dirty, notify entire module later.
					parent->markDirtyChild(this);
				}
				else
				{
					// classic method, notify pin immediatly.
					pluginParameters_GMPI->notifyPin(pinId, voice);
				}
			}

			if (pluginParameters)
			{
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
					pluginParametersLegacy->setPin(pinId, voice, size, (void*) data);
					pluginParametersLegacy->notifyPin(pinId, voice);
				}
			}

			// For outputs, send out notification to other connections (not the incoming one though).
			auto it = connections_.find(pinId);
			while (it != connections_.end() && (*it).first == pinId)
			{
				auto& connection = (*it).second;
				if (connection.otherModule_ != fromModule || connection.otherModulePinIndex_ != fromPinId)
				{
					connection.otherModule_->setPin(this, pinId, connection.otherModulePinIndex_, voice, size, data);
				}
				it++;
			}
			if (!initialised_)
			{
				//_RPT2(0, "m:%d alreadySentDataPins_ <- %d\n", handle, pinId);
				alreadySentDataPins_.push_back(pinId);
			}

			--recursionStopper_;
		}
		return gmpi::MP_OK;
	}

	void ModuleView::process()
	{
		dirty = false;
		if(pluginEditor2) // todo dirty flag for optimisation.
		{
			pluginEditor2->process();
		}
		else if (subView)
		{
			subView->process();
		}

		// todo SubViews recursivly
	}

	// works on structure, not on panel, panel sub-controls don't know if they are muted or not.
	bool ModuleView::isPinConnectionActive(int pinIndex)const
	{
		for (auto it = connections_.find(pinIndex) ; it != connections_.end() && (*it).first == pinIndex ; ++it)
		{
			auto& connection = (*it).second;
			if(!connection.otherModule_->isMuted())
			{
				return true;
			}
		}

		return false;
	}

	// not a normal deletion of entire view, but a selective deletion of one module.
	// remove all connection to prevent crashes when module tries to send data to already deleted module.
	void ModuleView::preDelete()
	{
		for (auto& it : connections_)
		{
			auto& connector = it.second;
			auto& other = *(connector.otherModule_);
			auto otherPinIdx = connector.otherModulePinIndex_;

			for (auto it = other.connections_.begin(); it != other.connections_.end(); ++it)
			{
				auto& connection = (*it).second;
				if (connection.otherModule_ == this && connection.otherModulePinIndex_ == connector.myPinIndex_)
				{
					it = other.connections_.erase(it);
					break;
				}
			}
		}

		connections_.clear();
	}

	std::string ModuleView::getToolTip(gmpi::drawing::Point point)
	{
		if (pluginGraphics2)
		{
			auto local = PointToPlugin(point);

			gmpi_sdk::MpString s;
			if( MP_OK == pluginGraphics2->getToolTip(*reinterpret_cast<GmpiDrawing_API::MP1_POINT*>(&local), &s) )
			{
				return s.str();
			}
			return string();
		}

		return string();
	}

	void ModuleView::receiveMessageFromAudio(void* msg)
	{
		struct DspMsgInfo
		{
			int id;
			int size;
			void* data;
		};
		const DspMsgInfo* nfo = (DspMsgInfo*)msg;

		if (pluginParameters)
		{
			pluginParameters->receiveMessageFromAudio(nfo->id, nfo->size, nfo->data);
		}
		if (pluginParametersLegacy)
		{
			pluginParametersLegacy->receiveMessageFromAudio(nfo->id, nfo->size, nfo->data);
		}
	}

	// adapt SDK3 to GMPI-UI context menu callback
	class ContextMenuAdaptor :  public gmpi::IMpContextItemSink
	{
		gmpi::api::IContextItemSink* sink{};
		gmpi::shared_ptr<gmpi::api::IUnknown> currentCallback;

	public:

		void setCallback(std::function<void(int32_t selectedId)> pcallback)
		{
			currentCallback = {};
			if(pcallback)
				currentCallback = new gmpi::sdk::PopupMenuCallback(pcallback);
		}

		ContextMenuAdaptor(gmpi::api::IUnknown* psink)
		{
			psink->queryInterface(&gmpi::api::IContextItemSink::guid, (void**)&sink);
		}

		// IMpContextItemSink
		int32_t MP_STDCALL AddItem(const char* text, int32_t id, int32_t flags = 0) override
		{
			return (int32_t) sink->addItem(text, id, flags, currentCallback.get());
		}

		GMPI_QUERYINTERFACE1(gmpi::MP_IID_CONTEXT_ITEMS_SINK, gmpi::IMpContextItemSink);
		GMPI_REFCOUNT_NO_DELETE;
	};

	gmpi::ReturnCode ModuleView::populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink)
	{
		if(false)
		{
			// TODO!!! GMPI-UI client support for context menu
		}
		else if (pluginParameters)
		{
			ContextMenuAdaptor menu(contextMenuItemsSink);

			menu.setCallback([this](int32_t selectedId)
			{
				pluginParameters->onContextMenu(selectedId);
			});

			menu.AddItem("", 0, (int32_t)gmpi::api::PopupMenuFlags::Separator);

			const auto local = PointToPlugin(point);

			pluginParameters->populateContextMenu(local.x, local.y, &menu);
		}

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode ModuleViewPanel::hitTest(gmpi::drawing::Point point, int32_t flags)
	{
		if (ModuleView::hitTest(point, flags) != gmpi::ReturnCode::Ok)
			return gmpi::ReturnCode::Unhandled;

		if (!pluginInput_GMPI && !pluginGraphics2)
			return gmpi::ReturnCode::Unhandled;

		auto local = PointToPlugin(point);

		if (pluginInput_GMPI)
			return pluginInput_GMPI->hitTest(*(gmpi::drawing::Point*) &local, flags);

		if (subView)
		{
			return subView->hitTest(flags, &local) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
		}
		else
		{
			if (pluginGraphics3)
			{
				// TODO!! use editEnabled to somehow ignore click on knob titles when no editing.
				// e.g. List entry and knobs on PD303 have blank area at top that blocks anything above from being clicked.
				// either add a flag, or a host-control ("is editor") to allow plugin to know if it's in edit mode.
				return pluginGraphics3->hitTest2(flags, *reinterpret_cast<GmpiDrawing_API::MP1_POINT*>(&local)) == gmpi::MP_OK ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
			}

			return pluginGraphics2->hitTest(*reinterpret_cast<GmpiDrawing_API::MP1_POINT*>(&local)) == gmpi::MP_OK ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
		}
	}

	gmpi::ReturnCode ModuleView::hitTest(gmpi::drawing::Point point, int32_t flags)
	{
		return isVisable() && pointInRect(point, getLayoutRect()) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ModuleView::setHover(bool mouseIsOverMe)
	{
		isHovered_ = mouseIsOverMe;

		if (pluginInput_GMPI)
			return pluginInput_GMPI->setHover(mouseIsOverMe);
		else if (pluginGraphics3) // TODO: implement a static dummy pluginGraphics2 to avoid all the null tests.
			return (gmpi::ReturnCode) pluginGraphics3->setHover(mouseIsOverMe);

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ModuleViewPanel::setHover(bool mouseIsOverMe)
	{
		const bool visualStateChanged = isHovered_ != mouseIsOverMe;

		auto r = ModuleView::setHover(mouseIsOverMe);

		if(visualStateChanged && BundleInfo::instance()->isEditor)
		{
			const auto r = inflateRect(bounds_, 2.f);
			parent->ChildInvalidateRect(r);
		}

		return r;
	}

	std::vector<patchpoint_description>* ModuleView::getPatchPoints()
	{
		return &moduleInfo->patchPoints;
	}

	// pointer event overrides are defined inline in ModuleView.h

	void ModuleView::OnCableDrag(ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistanceSquared, ModuleView*& bestModule, int& bestPinIndex)
	{
		if (dragline->type != CableType::PatchCable)
			return;

		auto point = dragPoint;
		if (hitTest(point, 0) == gmpi::ReturnCode::Ok)
		{
			gmpi::drawing::Point local = PointToPlugin(point);

			if (subView)
			{
				subView->OnCableDrag(dragline, local, bestDistanceSquared, bestModule, bestPinIndex);
			}
			else
			{
				std::vector<patchpoint_description>* patchPoints;
				patchPoints = &getModuleType()->patchPoints;

				const auto fixedEnd = dragline->fixedEnd();

				for (auto& patchpoint : *patchPoints)
				{
					float distanceSquared = (local.x - patchpoint.x) * (local.x - patchpoint.x) + (local.y - patchpoint.y) * (local.y - patchpoint.y);
					if (distanceSquared < bestDistanceSquared && (fixedEnd.module != handle || fixedEnd.index != patchpoint.dspPin))
					{
						if (Presenter()->CanConnect(dragline->type, fixedEnd.module, fixedEnd.index, handle, patchpoint.dspPin))
						{
							bestDistanceSquared = distanceSquared;
							bestModule = this;
							bestPinIndex = patchpoint.dspPin;
						}
					}
				}
			}
		}
	}

	bool ModuleViewPanel::EndCableDrag(gmpi::drawing::Point point, ConnectorViewBase* dragline, int32_t keyFlags)
	{
		if (hitTest(point, 0) != gmpi::ReturnCode::Ok)
			return false;

		gmpi::drawing::Point local(point);
		auto modulePosition = getLayoutRect();
		local.x -= modulePosition.left;
		local.y -= modulePosition.top;

		for (auto& patchpoint : getModuleType()->patchPoints)
		{
			float distanceSquared = (local.x - patchpoint.x) * (local.x - patchpoint.x) + (local.y - patchpoint.y) * (local.y - patchpoint.y);
			if (distanceSquared <= patchpoint.radius * patchpoint.radius)
			{
				auto toPin = patchpoint.dspPin;

				int colorIndex = 0;
				if (auto patchcable = dynamic_cast<PatchCableView*>(dragline); patchcable)
				{
					colorIndex = patchcable->getColorIndex();
				}

				const auto fromConnector = dragline->fmPin;
				Presenter()->AddPatchCable(fromConnector.module, fromConnector.index, getModuleHandle(), toPin, colorIndex);
				return true;
			}
		}

		return false;
	}

	bool ModuleViewPanel::isShown()
	{
		return parent->isShown();
	}

	bool ModuleViewPanel::hitTestR(int32_t flags, gmpi::drawing::Rect selectionRect)
	{
		if (!isVisable())
			return false;

		if(!ModuleView::hitTestR(flags, selectionRect))
			return false;

		// ignore hidden panels when selecting by lasso
		if (subView)
		{
			return subView->isVisible();
		}

		return true;
	}

	bool ModuleViewPanel::isDraggable(bool editEnabled)
	{
		// default is that anything can be dragged in the editor.
		return editEnabled || isRackModule();
	}

#if 0 // TODO

	// legacy crap forwarded to new members..
	int32_t ModuleView::pinTransmit(int32_t pinId, int32_t size, /*const*/ void* data, int32_t voice)
	{
		return pinTransmit(pinId, size, (const void*) data, voice);
	}
	int32_t ModuleView::sendMessageToAudio(int32_t id, int32_t size, /*const*/ void* messageData)
	{
		return sendMessageToAudio(id, size, (const void*) messageData);
	}

	// These legacy functions are supported asthey are simple.
	int32_t ModuleView::getHostId(int32_t maxChars, wchar_t* returnString)
	{
		const wchar_t* hostName = L"SynthEdit UWP";
#if defined( _MSC_VER )
		wcscpy_s(returnString, maxChars, hostName);
#else
		wcscpy(returnString, hostName);
#endif
		return gmpi::MP_OK;
	}

	int32_t ModuleView::getHostVersion(int32_t& returnValue)
	{
		returnValue = 1000;
		return gmpi::MP_OK;
	}

	// We don't support these legacy functions from older SDK.
	int32_t ModuleView::setIdleTimer(int32_t active)
	{
		return gmpi::MP_FAIL;
	}
	int32_t ModuleView::resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename)
	{
//		return parent->getGuiHost2()->resolveFilename(shortFilename, maxChars, returnFullFilename);
		return Presenter()->GetPatchManager()->resolveFilename(shortFilename, maxChars, returnFullFilename);
	}
	int32_t ModuleView::addContextMenuItem( /*const*/ wchar_t* menuText, int32_t index, int32_t flags)
	{
		return gmpi::MP_FAIL;
	}
	int32_t ModuleView::getPinCount(int32_t& returnCount)
	{
		if (totalPins_ > -1)
		{
			returnCount = totalPins_;
			return gmpi::MP_OK;
		}

		return gmpi::MP_FAIL;
	}

	int32_t ModuleView::openProtectedFile(const wchar_t* shortFilename, class IProtectedFile **file)
	{
		*file = nullptr; // If anything fails, return null.

		// new way, disk files only. (and no MFC).
//		std::wstring filename(shortFilename);
//		std::wstring l_filename = AudioMaster()->getShell()->ResolveFilename(filename, L"");

		const int maxChars = 512;
		wchar_t temp[maxChars];
		resolveFilename(shortFilename, maxChars, temp);

		wstring l_filename(temp);
		auto filename_utf8 = WStringToUtf8(l_filename);

		auto pf2 = ProtectedFile2::FromUri(filename_utf8.c_str());

		if (pf2)
		{
			*file = pf2;
			return gmpi::MP_OK;
		}

		return gmpi::MP_FAIL;
	}
#endif

	std::unique_ptr<IViewChild> ModuleViewPanel::createAdorner(ViewBase* pParent)
	{
		return std::make_unique<ResizeAdorner>(pParent, this);
	}

	} // namespace.

#if 0 //def SE_TAR GET_PURE_UWP
	void ModuleView::OnPointerPressed(float x, float y, Windows::UI::Xaml::Input::PointerRoutedEventArgs ^e)
	{
		float childLeft = position_.left + ((position_.right - position_.left) - pluginGraphicsSize.width) * 0.5f;

		gmpi::drawing::Point gmpiPoint(x - childLeft, y - (position_.bottom - pluginGraphicsSize.height));

		Windows::UI::Xaml::Input::Pointer^ ptr = e->Pointer;

		int32_t flags = 0;

		if (ptr->PointerDeviceType == Windows::Devices::Input::PointerDeviceType::Mouse)
		{
			flags |= gmpi_gui_api::GG_POINTER_FLAG_INCONTACT;
			// To get mouse state, we need extended pointer details.
			// We get the pointer info through the getCurrentPoint method
			// of the event argument. 
			Windows::UI::Input::PointerPoint^ ptrPt = e->GetCurrentPoint(nullptr);
			if (ptrPt->Properties->IsLeftButtonPressed)
			{
				flags |= gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY;
				//				eventLog.Text += "\nLeft button: " + ptrPt.PointerId;
			}
			if (ptrPt->Properties->IsMiddleButtonPressed)
			{
				//				eventLog.Text += "\nWheel button: " + ptrPt.PointerId;
				flags |= gmpi_gui_api::GG_POINTER_FLAG_THIRDBUTTON;
			}
			if (ptrPt->Properties->IsRightButtonPressed)
			{
				flags |= gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON;
			}
		}
		else
		{
			// pen or touch.

			// Guess.
			flags |= gmpi_gui_api::GG_POINTER_FLAG_INCONTACT;
			flags |= gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY;
		}

		pluginGraphics->onPointerDown(flags, gmpiPoint);

		/* todo
		if (mouseCaptured)
		{
		canvas->CapturePointer(ptr);
		}
		*/
	}

	void ModuleView::OnPointerMoved(float x, float y, Windows::UI::Xaml::Input::PointerRoutedEventArgs ^e)
	{
		float childLeft = position_.left + ((position_.right - position_.left) - pluginGraphicsSize.width) * 0.5f;
		gmpi::drawing::Point gmpiPoint(x - childLeft, y - (position_.bottom - pluginGraphicsSize.height));
		int32_t flags = 0;
		pluginGraphics->onPointerMove(flags, gmpiPoint);
	}

	void ModuleView::OnPointerReleased(float x, float y, Windows::UI::Xaml::Input::PointerRoutedEventArgs ^e)
	{
		float childLeft = position_.left + ((position_.right - position_.left) - pluginGraphicsSize.width) * 0.5f;
		gmpi::drawing::Point gmpiPoint(x - childLeft, y - (position_.bottom - pluginGraphicsSize.height));
		int32_t flags = 0;
		pluginGraphics->onPointerUp(flags, gmpiPoint);
	}
#endif
