#include <cmath>
#include <random>
#include <sstream>
#include <iostream>
#include "ViewBase.h"
#include "ConnectorView.h"
#include "modules/se_sdk3_hosting/Presenter.h"
#include "modules/se_sdk3_hosting/gmpi_drawing_conversions.h"
#include "ResizeAdorner.h"
#include "GuiPatchAutomator3.h"
#include "UgDatabase.h"
#include "modules/shared/unicode_conversion.h"
#include "RawConversions.h"
#include "IPluginGui.h"
#include "mfc_emulation.h"
#include "IGuiHost2.h"
#include "InterfaceObject.h"
#include "modules/se_sdk3_hosting/PresenterCommands.h"

#ifdef _WIN32
#include "Shared/DrawingFrame2_win.h"
#endif

// #define DEBUG_HIT_TEST
#define DEBUG_MOUSEOVER 0

using namespace std;
using namespace gmpi;
using namespace gmpi_gui;
using namespace gmpi::drawing;
using namespace GmpiGuiHosting;

namespace SE2
{
	ViewBase::ViewBase(gmpi::drawing::Size size) :
		drawingBounds{ 0, 0, size.width, size.height }
	{
	}

	void ViewBase::setDocument(SE2::IPresenter* ppresentor)
	{
		Init(ppresentor);
	}
#if 0 
	gmpi::ReturnCode ViewBase::open(gmpi::api::IUnknown* host)
	{
		if (inputHost_)
		{
			inputHost_->release();
			inputHost_ = {};
		}

		if (drawingHost_)
		{
			drawingHost_->release();
			drawingHost_ = {};
		}

		host->queryInterface(&gmpi::api::IDrawingHost::guid, reinterpret_cast<void**>(&drawingHost_));
		host->queryInterface(&gmpi::api::IInputHost::guid, reinterpret_cast<void**>(&inputHost_));

#if defined(_WIN32)
		frameWindow = dynamic_cast<DrawingFrameBase2*>(host);
#endif

		return drawingHost_ ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::NoSupport;
	}
#endif 
	gmpi::ReturnCode ViewBase::render(gmpi::drawing::api::IDeviceContext* drawingContext)
	{
		Graphics g(drawingContext);

		// Restrict drawing only to overall clip-rect.
		auto cliprect = g.getAxisAlignedClip();
		//_RPT4(_CRT_WARN, "OnRender    clip[ %d %d %d %d]\n", (int)cliprect.left, (int)cliprect.top, (int)cliprect.right, (int)cliprect.bottom);

		const Matrix3x2 originalTransform = g.getTransform();

		// Cache the list of children inside the clip rectangle that support layered rendering.
		// This avoids redundant clip-rect and interface checks across the 3 layer passes.
		struct LayeredChild
		{
			IViewChild* child;
			Matrix3x2 transform;
		};
		std::vector<LayeredChild> layeredChildren;
		for (auto& m : children)
		{
			if (!m->hasRenderLayers())
				continue;

			auto b = m->getClipArea();
			if (!overlaps(b, cliprect))
				continue;

			auto layoutRect = m->getLayoutRect();
			layeredChildren.push_back({ m.get(), makeTranslation(layoutRect.left, layoutRect.top) * originalTransform });
		}

		// Pass 1: layer -2 (background) - only layer-supporting plugins
		for(auto& lc : layeredChildren)
		{
			g.setTransform(lc.transform);
			lc.child->renderPluginLayer(g, -2);
		}

		// Pass 2: layer -1 (shadows) - only layer-supporting plugins
		for(auto& lc : layeredChildren)
		{
			g.setTransform(lc.transform);
			lc.child->renderPluginLayer(g, -1);
		}

		// Pass 3: normal render (all children in clip rect)
		// Layer-supporting plugins render layer 0 inside their render() method.
		// Non-layer plugins render normally via render().
		bool isOriginal = false;
		g.setTransform(originalTransform);
		for(auto& m : children)
		{
			auto b = m->getClipArea();
			if(!overlaps(b, cliprect))
				continue;

			if(dynamic_cast<ConnectorViewBase*>(m.get()))
			{
				if(!isOriginal) // avoid repeated set of original transform if not needed.
				{
					g.setTransform(originalTransform);
					isOriginal = true;
				}
			}
			else
			{
				auto layoutRect = m->getLayoutRect();
				auto adjustedTransform = makeTranslation(layoutRect.left, layoutRect.top) * originalTransform;
				g.setTransform(adjustedTransform);
				isOriginal = false;
			}

			m->render(g);
		}

		// Pass 4: layer 1 (glow) - only layer-supporting plugins
		for (auto& lc : layeredChildren)
		{
			g.setTransform(lc.transform);
			lc.child->renderPluginLayer(g, 1);
		}

		g.setTransform(originalTransform);

		return gmpi::ReturnCode::Ok;
	}

	// all clicks are hit on a view.
	// legacy host-facing hit-test entry point.
	gmpi::ReturnCode ViewBase::hitTest(gmpi::drawing::Point point, int32_t flags)
	{
		return gmpi::ReturnCode::Ok;
		/* ?
		(void)flags;
		auto p = point;
		return Find(p) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
		*/
	}

	gmpi::ReturnCode ViewBase::onPointerDown(gmpi::drawing::Point point, int32_t flags)
	{
		point = point * inv_viewTransform;

#ifdef DEBUG_HIT_TEST
		_RPT3(0, "ViewBase::onPointerDown(%x, (%f, %f))\n", flags, point.x, point.y);
#endif
		Presenter()->NotDragging();

		// handle edge-case of mouse clicking without any prior 'OnMove' (e.g. after clicking to make a pop-up menu disappear).
		// ensures that 'mouseOverObject' is correct.
		if(lastMovePoint.x != point.x || lastMovePoint.y != point.y)
		{
			// clear out click-related flags.
			const auto simulatedFlags = flags &
				~(
					gmpi_gui_api::GG_POINTER_FLAG_INCONTACT |
					gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON |
					gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON |
					gmpi_gui_api::GG_POINTER_FLAG_THIRDBUTTON |
					gmpi_gui_api::GG_POINTER_FLAG_FOURTHBUTTON
					);

			onPointerMove(point, simulatedFlags);
		}
        
		if(mouseCaptureObject)
		{
#ifdef DEBUG_HIT_TEST
			_RPT1(0, "mouseCaptureObject=%x\n", mouseCaptureObject);
#endif

			/*return*/ mouseCaptureObject->onPointerDown(point, flags);
			return gmpi::ReturnCode::Ok;
		}
        
        // if we're dragging a module from the browser, suppress normal stuff, otherwise weird conditions happen.
        if(!draggingNewModuleId.empty())
        {
//            setCapture();
            return gmpi::ReturnCode::Handled;
        }

		// account for objects appearing without mouse moving (e.g. show-on-parent changing on previous click).
		calcMouseOverObject(flags);

		IViewChild* hitObject = nullptr;
		if(mouseOverObject)
		{
			auto result = mouseOverObject->onPointerDown(point, flags);
			// Module has captured mouse. Let it take over.
			if(mouseCaptureObject)
			{
#ifdef DEBUG_HIT_TEST
				_RPT0(0, " and captured\n");
#endif
				return gmpi::ReturnCode::Ok;
			}

			// module didn't capture mouse, drag object around.
			if(result == gmpi::ReturnCode::Ok) // Ok indicates mouse 'hit'
				hitObject = mouseOverObject;

#if 1		// object handles click itself but without capturing mouse. e.g. clicking a line.
			if(result == gmpi::ReturnCode::Handled) // indicates mouse 'hit' AND handled already.
			{
#ifdef DEBUG_HIT_TEST
				_RPT0(0, " and not captured\n");
#endif
				return result; // no further handling needed.
			}
#ifdef DEBUG_HIT_TEST
			_RPT0(0, "\n");
#endif
#endif
		}

		if(modulePicker && hitObject != modulePicker)
			DismissModulePicker();

		// Mouse 'hit' module, but module did not capture it. Drag module if selected.
		if(hitObject)
		{
			// Left-click (drag object)
			if((flags & gmpi_gui_api::GG_POINTER_FLAG_NEW) != 0 &&
				(flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0 &&
				hitObject->getSelected() &&
				hitObject->isDraggable(Presenter()->editEnabled())
				)
			{
#ifdef DEBUG_HIT_TEST
				_RPT0(0, "Dragging Object\n");
#endif
				pointPrev = point;

				isDraggingModules = true;
				DraggingModulesOffset = {};
				DraggingModulesInitialTopLeft = { hitObject->getLayoutRect().left, hitObject->getLayoutRect().top };
				DraggingObject = hitObject;

				if(inputHost)
					inputHost->setCapture();
				autoScrollStart();
			}
		}
		else
		{
#ifdef DEBUG_HIT_TEST
			_RPT0(0, "Nothing hit \n");
#endif

			// Nothing hit, clear selection (left click only).
			if((flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0)
			{
				Presenter()->ObjectClicked(-1, flags); //gmpi::modifier_keys::getHeldKeys());

				if(Presenter()->editEnabled())
				{
					assert((flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0); // Drag selection box.
					assert(!isIteratingChildren);
					children.push_back(std::unique_ptr<IViewChild>(new SelectionDragBox(this, point)));
					autoScrollStart();
					return gmpi::ReturnCode::Ok;
				}
			}
		}

		// indicate successful hit.
		return hitObject ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ViewBase::onPointerMove(gmpi::drawing::Point point, int32_t flags)
	{
#if DEBUG_MOUSEOVER
		_RPTN(0, "ViewBase::onPointerMove: [%f,%f]\n", ppoint.x, ppoint.y);
#endif
		currentPointerPosAbsolute = point;
		lastMovePoint = currentPointerPosAbsolute * inv_viewTransform;

		if(mouseCaptureObject)
		{
#if DEBUG_MOUSEOVER
			_RPTN(0, "mouseCaptureObject->onPointerMove() : %s\n", typeid(*mouseCaptureObject).name());
#endif
			mouseCaptureObject->onPointerMove(lastMovePoint, flags);
		}
		else
		{
			if(isDraggingModules) // could this be handled with custom mouseCaptureObject? to remove need for check here?
			{
				// Snap-to-grid logic.
				gmpi::drawing::Size delta(lastMovePoint.x - pointPrev.x, lastMovePoint.y - pointPrev.y);
				if(delta.width != 0.0f || delta.height != 0.0f) // avoid false snap on selection
				{
					const auto snapGridSize = Presenter()->GetSnapSize();

					gmpi::drawing::Point dragModuleTopLeft = DraggingModulesInitialTopLeft + DraggingModulesOffset;
					gmpi::drawing::Point newPoint = dragModuleTopLeft + delta;

					newPoint.x = floorf((snapGridSize / 2 + newPoint.x) / snapGridSize) * snapGridSize;
					newPoint.y = floorf((snapGridSize / 2 + newPoint.y) / snapGridSize) * snapGridSize;
					auto snapDelta = newPoint - dragModuleTopLeft;

					pointPrev += snapDelta;

					if(snapDelta.width != 0.0 || snapDelta.height != 0.0)
					{
						Presenter()->DragSelection(snapDelta);
						DraggingModulesOffset += snapDelta;
					}
				}
				return gmpi::ReturnCode::Ok;
			}

            calcMouseOverObject(flags);

            if(mouseOverObject)
                mouseOverObject->onPointerMove(lastMovePoint, flags);
        }

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode ViewBase::onPointerUp(gmpi::drawing::Point point, int32_t flags)
	{
		point *= inv_viewTransform;

		Presenter()->NotDragging();
        
        if(mouseCaptureObject)
        {
            mouseCaptureObject->onPointerUp(point, flags);
            
#ifdef _DEBUG
            if(mouseCaptureObject)
            {
                _RPT0(_CRT_WARN, "WARNING: GUI MODULE DID NOT RELEASE MOUSECAPTURE!!!\n");
            }
#endif
        }
		else if(!draggingNewModuleId.empty())
		{
			/* mouse can only be captured by the window that ecieved the mpointer-down, and then only until the next pointer-up.
			int32_t isMouseCaptured{};
			getGuiHost()->getCapture(isMouseCaptured);
			if (isMouseCaptured)
			*/
			{
				if(inputHost)
					inputHost->releaseCapture();
				const auto moduleId = Utf8ToWstring(draggingNewModuleId); // TODO why does this get converted to UTF8 then back again?
				Presenter()->AddModule(moduleId.c_str(), point);
			}
			draggingNewModuleId.clear();
			if (onDragNewModuleEnded)
				onDragNewModuleEnded();
		}
        else if(isDraggingModules)
		{
			isDraggingModules = false;
			releaseCapture();
			autoScrollStop();

			if(DraggingObject && DraggingModulesOffset.width == 0.f && DraggingModulesOffset.height == 0.f)
			{
				// we clicked a module but didn't drag it. Perhaps user indended to auto-trace a pin?
				DraggingObject->OnClickedButDidntDrag();

				DraggingObject = {};
			}
		}
		
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode ViewBase::onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta)
	{
		if(isDraggingModules)
			return gmpi::ReturnCode::Unhandled;

		currentPointerPosAbsolute = point;
		point *= inv_viewTransform;

		const bool hasScrollbars = hscrollBar && vscrollBar;

		// <ALT> causes scroll wheel events to pass to client
		if((flags & gmpi_gui_api::GG_POINTER_KEY_ALT) || !hasScrollbars)
		{
			calcMouseOverObject(flags);

			if(!mouseOverObject)
				return gmpi::ReturnCode::Unhandled;

			mouseOverObject->onMouseWheel(point, flags, delta);
			return gmpi::ReturnCode::Ok;
		}

		// <CTRL> wheel = zoom
		if(flags & gmpi_gui_api::GG_POINTER_KEY_CONTROL)
		{
			// Compute the document point under the mouse before zoom changes.
			const auto mouseDocPos = currentPointerPosAbsolute * inv_viewTransform;

			if(delta < 0)
				zoomFactor /= 1.25f;
			else
				zoomFactor *= 1.25f;

			zoomFactor = std::clamp(zoomFactor, 0.1f, 10.0f);

			// Compute the snapped zoom that calcViewTransform will actually use,
			// so centerPos keeps the doc point exactly under the mouse.
			constexpr float gridDips = 12.0f;
			const float dpiScale = drawingHost ? drawingHost->getRasterizationScale() : 1.0f;
			const float snappedZoom = std::round(zoomFactor * gridDips * dpiScale) / (gridDips * dpiScale);

			const float viewWidth  = drawingBounds.right  - drawingBounds.left;
			const float viewHeight = drawingBounds.bottom - drawingBounds.top;
			centerPos.x = mouseDocPos.x + (viewWidth  * 0.5f - currentPointerPosAbsolute.x) / snappedZoom;
			centerPos.y = mouseDocPos.y + (viewHeight * 0.5f - currentPointerPosAbsolute.y) / snappedZoom;

			Presenter()->SetPanZoom(centerPos, zoomFactor);
		}
		else
		{
			// shift+wheel = horizontal scroll, plain wheel = vertical scroll.
			// delta is in pixels; convert to doc coords.
			constexpr float pixelsPerDetent = 0.25f; // 120 delta per wheel detent
			if(flags & gmpi_gui_api::GG_POINTER_KEY_SHIFT)
				centerPos.x -= static_cast<float>(delta) * pixelsPerDetent / zoomFactor;
			else
				centerPos.y -= static_cast<float>(delta) * pixelsPerDetent / zoomFactor;

			Presenter()->SetViewCenter(centerPos);
		}

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode ViewBase::populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink)
	{
		Presenter()->NotDragging();

		gmpi::shared_ptr<gmpi::api::IContextItemSink> menu;
		contextMenuItemsSink->queryInterface(&gmpi::api::IContextItemSink::guid, menu.put_void());

		auto moduleHandle = mouseOverObject ? mouseOverObject->getModuleHandle() : -1; // -1 = no module under mouse.
		Presenter()->populateContextMenu(menu, point, moduleHandle);

		if(mouseOverObject)
		{
			//			menu.populateFromObject(x, y, mouseOverObject);

			//menu.currentCallback =
			//	[this](int32_t idx)
			//	{
			//		return mouseOverObject->vc_onContextMenu(idx);
			//	};

			mouseOverObject->populateContextMenu(point, menu);

		}
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode ViewBase::onContextMenu(int32_t idx)
	{
		return ReturnCode::Unhandled;
	}

#if 0
	gmpi::ReturnCode ViewBase::populateContextMenu2(gmpi::api::IContextItemSink* menu, gmpi::drawing::Point point)
	{
		Presenter()->NotDragging();

		auto moduleHandle = mouseOverObject ? mouseOverObject->getModuleHandle() : -1; // -1 = no module under mouse.
		Presenter()->populateContextMenu(menu, point, moduleHandle);

		//gmpi::shared_ptr<gmpi::api::IContextItemSink> menu;
		//contextMenuItemsSink->queryInterface(&gmpi::api::IContextItemSink::guid, menu.put_void());
		//if(!menu)
		//	return gmpi::ReturnCode::NoSupport;



		GmpiSdk::ContextMenuHelper menu(contextMenuCallbacks, contextMenuItemsSink);

		// Add items for Presenter
		// Cut, Copy, Paste etc.
		menu.currentCallback =
			[this](int32_t idx)
			{
				return Presenter()->onContextMenu(idx);
			};

		auto moduleHandle = mouseOverObject ? mouseOverObject->getModuleHandle() : -1; // -1 = no module under mouse.
		Presenter()->populateContextMenu(&menu, { x, y }, moduleHandle);

		if(mouseOverObject)
		{
			//			menu.populateFromObject(x, y, mouseOverObject);

			menu.currentCallback =
				[this](int32_t idx)
				{
					return mouseOverObject->vc_onContextMenu(idx);
				};

			mouseOverObject->populateContextMenu(x, y, &menu);

		}
		return gmpi::ReturnCode::Ok;
	}
#endif

	//gmpi::ReturnCode ViewBase::onContextMenu(int32_t idx)
	//{
	//	return gmpi::ReturnCode::Ok; //static_cast<gmpi::ReturnCode>(GmpiSdk::ContextMenuHelper::onContextMenu(contextMenuCallbacks, idx));
	//}

	gmpi::ReturnCode ViewBase::onKeyPress(wchar_t c)
	{
		return onKey(c, &lastMovePoint);
	}

	void ViewBase::invalidateRect(const gmpi::drawing::Rect* invalidRect)
	{
		if (!drawingHost)
			return;

		const auto rect = invalidRect ? *invalidRect : drawingBounds;
		drawingHost->invalidateRect(&rect);
	}

	void ViewBase::DoClose()
	{
#if defined (_WIN32)
		if(frameWindow)
			SendMessage(frameWindow->getWindowHandle(), WM_CLOSE, 0, 0);
#endif
	}

	void ViewBase::Init(class IPresenter* ppresentor)
	{
		presenter.reset(ppresentor);
		presenter->setView(this);
	}

	void ViewBase::BuildPatchCableNotifier(std::map<int, class ModuleView*>& guiObjectMap)
	{
		// Need notification of HC_PATCH_CABLES updates.
		ModuleViewPanel* patchCableNotifier = nullptr;
		{
			auto ob = std::make_unique<ModuleViewPanel>(L"SE PatchCableChangeNotifier", this, Presenter()->GenerateTemporaryHandle());
			patchCableNotifier = ob.get();
			guiObjectMap.insert({ ob->getModuleHandle(), ob.get() });
			assert(!isIteratingChildren);
			children.push_back(std::move(ob));
		}

		// Hook up host-connect manually.
		{
			int hostConnect = HC_PATCH_CABLES;
			int32_t attachedToHandle = -1; // = not attached.

			auto patchAutomator = dynamic_cast<GuiPatchAutomator3*>(getPatchAutomator(guiObjectMap)->getpluginParameters());

			int pmPinIdx = patchAutomator->Register(attachedToHandle, -1 - hostConnect, FT_VALUE); // PM->Plugin.
			const int pinId = 0;
			patchAutomatorWrapper_->AddConnection(pmPinIdx, patchCableNotifier, pinId);
			patchCableNotifier->AddConnection(pinId, patchAutomatorWrapper_, pmPinIdx);				// plugin->PM.
		}
	}

	ModuleViewPanel* ViewBase::getPatchAutomator(std::map<int, class ModuleView*>& guiObjectMap)
	{
		if(patchAutomatorWrapper_ == nullptr)
		{
			// Insert invisible objects for preset management etc.
			// Patch Manager.
			{
				auto ob = std::make_unique<ModuleViewPanel>(L"PatchAutomator", this, Presenter()->GenerateTemporaryHandle());
				patchAutomatorWrapper_ = ob.get();
				auto r = guiObjectMap.insert({ ob->getModuleHandle(), ob.get() });
				assert(r.second);

				assert(!isIteratingChildren);
				children.push_back(std::move(ob)); // ob now null.
			}

			auto patchAutomator = dynamic_cast<GuiPatchAutomator3*>(patchAutomatorWrapper_->getpluginParameters());
			patchAutomator->Sethost(Presenter()->GetPatchManager());
		}

		return patchAutomatorWrapper_;
	}

	void ViewBase::ConnectModules(const Json::Value& context, std::map<int, class ModuleView*>& guiObjectMap)//, ModuleView* patchAutomatorWrapper)
	{
		const int32_t containerHandle = context["handle"].asInt();

		std::vector< std::pair< SE2::ModuleView*, int> > connectedInputs;

		// Step 1: Passively connect wires (no notification).
		// Doing this first allows defaults to be set strictly on only pins without wires.
		const Json::Value& modules_json = context["connections"];
		for(auto& lineElement : modules_json)
		{
			// or quicker to iterate?
//			int fromModule = -1;
//			int toModule = -1;
			auto fromModuleH = lineElement["fMod"].asInt();
			auto toModuleH = lineElement["tMod"].asInt();

			auto from = Presenter()->HandleToObject(fromModuleH); // !!! design problem, what if two objects have same handle (module + adorner)?
			auto to = Presenter()->HandleToObject(toModuleH);

			if(from && to) // are not muted.
			{
				int fromPinIndex = 0; // default if not specified.
				int toPinIndex = 0;
				fromPinIndex = lineElement["fPin"].asInt();
				toPinIndex = lineElement["tPin"].asInt();

				from->AddConnection(fromPinIndex, to, toPinIndex);
				to->AddConnection(toPinIndex, from, fromPinIndex);

				connectedInputs.push_back(std::make_pair(to, toPinIndex));
			}
		}

		// STEP 2: Set pin defaults.

		// Iterate GUI modules.
		const Json::Value& modules_element = context["modules"];

		if(!modules_element.empty())
		{
			for(auto it = modules_element.begin(); it != modules_element.end(); ++it)
			{
				const auto& module_element = *it; // get actual pointer to json object (not a copy).

				int handle = 0;
				handle = module_element["handle"].asInt();
				std::string pluginType = module_element["type"].asString();

				Module_Info* moduleInfo;
				if(pluginType == "Container")
				{
					pluginType = "ContainerX"; // so GUI pin defaults handled correctly.
					ConnectModules(module_element, guiObjectMap);
				}

				moduleInfo = CModuleFactory::Instance()->GetById(JmUnicodeConversions::Utf8ToWstring(pluginType));

				auto wrapper = Presenter()->HandleToObject(handle);

				if(wrapper && moduleInfo) // should not usually be null, but avoid crash if it is.
				{
					const auto& pins_element = module_element["Pins"];

					std::vector<int> alreadSetDefault;
					if(!pins_element.isNull())
					{
						// Set GUI pin defaults.
						int pinId = 0;
						for(auto& pin_element : pins_element)
						{
							const auto& idx_e = pin_element["Idx"]; // indicates DSP Pin.
							if(idx_e.isNull())
							{
								const auto& id_e = pin_element["Id"];
								if(!id_e.isNull())
									pinId = id_e.asInt();

								// Set pin defaults.
								auto& default_element = pin_element["default"];
								if(!default_element.empty())
								{
									InterfaceObject* pinInfo{};

									// find the pin description for this pin.
									for(auto& pd : moduleInfo->gui_plugs)
									{
										if (pd.second->getPlugDescID() == pinId)
										{
											pinInfo = pd.second;
											break;
										}
									}

									// Can't find desc? assume it's an auto-duplicating pin.
									if(!pinInfo && !moduleInfo->gui_plugs.empty())
									{
										auto& last = *moduleInfo->gui_plugs.rbegin();

										if (last.second->autoDuplicate())
										{
											pinInfo = last.second;
										}
									}

									if (pinInfo)
									{
										assert(pinInfo->GetDirection() == DR_OUT || !wrapper->isPinConnected(pinId) || !wrapper->isPinConnectionActive(pinId)); // can be connected to muted/unavailable module.

										auto dt = pinInfo->GetDatatype();
										if (dt == DT_ENUM) // special hack for enum lists on properties of GUI modules.
											dt = DT_INT;

										const auto raw = ParseToRaw(dt, default_element.asString());

										wrapper->setPin(0, 0, pinId, 0, (int32_t)raw.size(), (void*)raw.data());

										alreadSetDefault.push_back(pinId);
									}

#if 0
									auto def = default_element.asString();
									for(auto it2 = moduleInfo->gui_plugs.begin(); it2 != moduleInfo->gui_plugs.end(); ++it2)
									{
										auto& pinInfo = *(*it2).second;
										if(pinInfo.getPlugDescID() == pinId)
										{
											auto dt = pinInfo.GetDatatype();
											if(dt == DT_ENUM) // special hack for enum lists on properties of GUI modules.
											{
												dt = DT_INT;
											}

											auto raw = ParseToRaw(dt, def);

											assert(pinInfo.GetDirection() == DR_OUT || !wrapper->isPinConnected(pinId) || !wrapper->isPinConnectionActive(pinId)); // can be connected to muted/unavailable module.
											wrapper->setPin(0, 0, pinInfo.getPlugDescID(), 0, (int)raw.size(), (void*)(&raw[0]));

											alreadSetDefault.push_back(pinId);
											break;
										}
									}
#endif
								}

								++pinId; // Allows pinIdx to default to 1 + prev Idx. TODO, only used by slider2, could add this to exportXml.
							}
						}
					}

					// Standard defaults etc from module info.
					for(auto& plugInfoPair : moduleInfo->gui_plugs)
					{
						auto& pinInfo = *plugInfoPair.second;
						int pinId = pinInfo.getPlugDescID();

						// Default.
						if(pinInfo.GetDirection() == DR_IN)
						{
							wrapper->inputPinIds.push_back(pinId); // Cheap way of noting pin directions for correct feedback behaviour.

							if(!wrapper->isPinConnectionActive(pinId))
							{
								if(std::find(alreadSetDefault.begin(), alreadSetDefault.end(), pinId) == alreadSetDefault.end())
								{
									auto dt = pinInfo.GetDatatype();
									if(dt == DT_ENUM) // special hack for enum lists on properties of GUI modules.
									{
										dt = DT_INT;
									}
									auto raw = ParseToRaw(dt, pinInfo.GetDefaultVal());
									wrapper->setPin(0, 0, pinId, 0, (int)raw.size(), (void*)(&raw[0]));
								}
							}
						}

						if(pinInfo.isParameterPlug() || pinInfo.isHostControlledPlug())
						{
							int pmPinIdx = -1;
							auto patchAutomatorWrapper = wrapper->parent->getPatchAutomator(guiObjectMap);
							auto patchAutomator = dynamic_cast<GuiPatchAutomator3*>(patchAutomatorWrapper->getpluginParameters());

							//	bool isPolyphonic = false; // TODO.

							// Parameter.
							if(pinInfo.isParameterPlug())
							{
								pmPinIdx = patchAutomator->Register(handle, pinInfo.getParameterId(), (ParameterFieldType)pinInfo.getParameterFieldId()); // PM->Plugin.
							}
							else // Host-control.
							{
								assert(pinInfo.isHostControlledPlug());

								const int hostConnect = pinInfo.getHostConnect(); // pass as negative field to identify it as host-connect.

								int32_t attachedToHandle = -1; // = not attached.
								if(AttachesToVoiceContainer((HostControls)hostConnect))
								{
									attachedToHandle = module_element["VoiceContainer"].asInt();
								}
								else if (AttachesToParentContainer((HostControls)hostConnect))
								{
									attachedToHandle = containerHandle;
								}
								pmPinIdx = patchAutomator->Register(attachedToHandle, -1 - hostConnect, (ParameterFieldType)pinInfo.getParameterFieldId()); // PM->Plugin.
							}

							if(pmPinIdx != -1) // not available.
							{
								patchAutomatorWrapper->AddConnection(pmPinIdx, wrapper, pinId);
								wrapper->AddConnection(pinId, patchAutomatorWrapper, pmPinIdx);		// plugin->PM.
							}
						}
					}

					// -1 not recorded/relevant. Only for auto-duplicating.
					const auto& pins_count = module_element["PinCount"];
					if(!pins_count.isNull())
					{
						wrapper->setTotalPins(pins_count.asInt());

						// Might be needed on input pins for correct feedback behavior.
						/* something like, for each pin.
						if (pinInfo.GetDirection() == DR_IN)
							wrapper->inputPinIds.push_back(pinId);
						*/
					}
				}
			}
		}

		auto containerXmoduleInfo = CModuleFactory::Instance()->GetById(L"ContainerX"); // so GUI pin defaults handled correctly.
		const int64_t defaultPinValue = 0;

		// STEP3 : Push pin values down wires.
		for(auto& inputPin : connectedInputs)
		{
			auto to = inputPin.first;
			auto toPinIndex = inputPin.second;

			auto it = to->connections_.find(toPinIndex);
			if(it != to->connections_.end())
			{
				auto& connection = (*it).second;
				auto fromPinIndex = connection.otherModulePinIndex_;
				auto from = connection.otherModule_;

				// Has that module already output a value on the outgoing pin?
				// If so, pass it on.
				assert(!from->initialised_);
				if(from->alreadySentDataPins_.end() == std::find(from->alreadySentDataPins_.begin(), from->alreadySentDataPins_.end(), fromPinIndex))
				{
					// Module hasn't sent anything yet.
					// When output pin is the default value (0), module won't ever send anything (then to-pin NEVER gets updated).
					// In this case send pin's default value. ('from' module can still override it later during initialisation).
					auto moduleInfo = to->getModuleType();
					
					if (!moduleInfo) // skip missing modules
						continue;

					if(moduleInfo->UniqueId() == L"Container")
					{
						moduleInfo = containerXmoduleInfo;
					}

					auto safePinIndex = (std::min)(toPinIndex, moduleInfo->GuiPlugCount() - 1); // Autoduplicating assume last index.
					if(safePinIndex >= 0) // ignore legacy SDK2 modules like "Bools to List"
					{
						auto datatype = moduleInfo->getGuiPinDescriptionByPosition(safePinIndex)->GetDatatype();
						to->setPin(from, fromPinIndex, toPinIndex, 0, getDataTypeSize(datatype), (void*)&defaultPinValue);
					}
				}
			}
		}
	}

	int32_t ViewBase::setCapture(IViewChild* module)
	{
		//		assert(dynamic_cast<SE2::PatchCableView*>(module) == 0);
		mouseCaptureObject = module;
		return inputHost ? static_cast<int32_t>(inputHost->setCapture()) : gmpi::MP_FAIL;
	}

	int32_t ViewBase::releaseCapture()
	{
		mouseCaptureObject = nullptr;
		auto r = inputHost ? static_cast<int32_t>(inputHost->releaseCapture()) : gmpi::MP_FAIL;

		calcMouseOverObject(0);

		return r;
	}

#if 0 // TODO support this
	int32_t ViewBase::getToolTip(gmpi::drawing::Point point, gmpi::api::IString* returnString)
	{
		std::string returnToolTip;

		for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
		{
			auto& m = *it;
			if(m->hitTest(0, point) == gmpi::ReturnCode::Ok)
			{
				returnToolTip = m->getToolTip(point);
				break;
			}
		}

		if (returnToolTip.empty() ) // || MP_OK != returnString->queryInterface(gmpi::MP_IID_RETURNSTRING, reinterpret_cast<void**>(&returnValue)))
		{
			return gmpi::MP_NOSUPPORT;
		}

		returnString->setData(returnToolTip.data(), (int32_t)returnToolTip.size());
		return gmpi::ReturnCode::Ok;
	}
#endif

	void ViewBase::RemoveChild(IViewChild* child)
	{
		if (mouseOverObject == child)
		{
			mouseOverObject = nullptr;
		}

		for (auto it = children.begin(); it != children.end(); ++it)
		{
			if ((*it).get() == child)
			{
				assert(!isIteratingChildren);
				children.erase(it);
				break;
			}
		}
	}

	void ViewBase::OnDragSelectionBox(int32_t flags, gmpi::drawing::Rect selectionRect)
	{
		// can't select them while iterating because fresh adorners invalidate vector.
		std::vector<int32_t> modulesToSelect;

		// MODULES
		for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
		{
			auto& m = *it;

			if (m->hitTestR(flags, selectionRect))
			{
				modulesToSelect.push_back(m->getModuleHandle());
			}
		}

		for(auto h : modulesToSelect)
			Presenter()->ObjectSelect(h);
	}

	gmpi::ReturnCode ViewBase::setHover(bool isMouseOverMe)
	{
		if (!isMouseOverMe && mouseOverObject)
		{
			mouseOverObject->setHover(false);
			mouseOverObject = {};

			return gmpi::ReturnCode::Ok;
		}

		return gmpi::ReturnCode::Unhandled;
	}

	void ViewBase::onSubPanelMadeVisible()
	{
		// if a sub-panel just blinked into existence, need to update mouse over object on myself AND on it. Else the next click will be ignored.
		// calling ViewBase to avoid the offset imposed by the sub-panel (which has already been accounted for)
		ViewBase::onPointerMove(lastMovePoint, 0);
	}

	void ViewBase::calcMouseOverObject(int32_t flags)
	{
#if DEBUG_MOUSEOVER
		_RPT0(0, "\n\nViewBase::calcMouseOverObject()\n");
#endif
		// when one object has captured mouse, don't highlight other objects.
		if (mouseCaptureObject)
		{
#if DEBUG_MOUSEOVER
			_RPT0(0, "Mouse already captured. exit.\n");
#endif
			return;
		}

		IViewChild* hitObject{};
		float bestScore = 25.0f;	// maximum distance from mouse pointer to be considered a 'hit' (e.g. for hitting pins slightly off-target).
									// objects can impose a lower threshhold. e.g. structureview is 12 pixels max.

		isIteratingChildren = true;
		for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
		{
			auto& m = *it;
#if DEBUG_MOUSEOVER
#endif
			auto score = m->hitTestFuzzy(flags, lastMovePoint);

#if DEBUG_MOUSEOVER
			if (score < 30.f)
			{
				_RPTN(0, "%s : %f pixels\n", typeid(*m.get()).name(), score);
			}
#endif

			if(score < bestScore) // 'hard' hit test-> && score == 0.0f)
			{
#if DEBUG_MOUSEOVER
				_RPTN(0, "HIT: %s\n", typeid(*m.get()).name());
#endif
				hitObject = m.get();
				bestScore = score;
			}
			else
			{
//#if DEBUG_MOUSEOVER
//				_RPTN(0, "MISS: %s\n", typeid(*m.get()).name());
//#endif
			}
		}
		isIteratingChildren = false;

		if(hitObject != mouseOverObject)
		{
			if(mouseOverObject)
				mouseOverObject->setHover(false);

			mouseOverObject = hitObject;

			if(mouseOverObject)
				mouseOverObject->setHover(true);
		}
	}

	void ViewBase::OnChildDeleted(IViewChild* childObject)
	{
		if (mouseOverObject == childObject)
		{
			mouseOverObject = nullptr;
		}
	}

	void ViewBase::autoScrollStart()
	{
		isAutoScrolling = true;
		startTimer(24); // ms
	}

	void ViewBase::autoScrollStop()
	{
		isAutoScrolling = false;
	}

	bool ViewBase::onTimer()
	{
		if (!isAutoScrolling)
			return false;

//		_RPTN(0, "AutoScroll [%f, %f]\n", currentPointerPosAbsolute.x, currentPointerPosAbsolute.y);

		float autoScrolDx = (std::max)(0.0f, -currentPointerPosAbsolute.x) - (std::max)(0.0f, currentPointerPosAbsolute.x - (drawingBounds.right - drawingBounds.left));
		float autoScrolDy = (std::max)(0.0f, -currentPointerPosAbsolute.y) - (std::max)(0.0f, currentPointerPosAbsolute.y - (drawingBounds.bottom - drawingBounds.top));

		constexpr float maxSpeed = 22.f; // pixels per timer tick (24ms)
		autoScrolDx = std::clamp(autoScrolDx * 0.5f, -maxSpeed, maxSpeed);
		autoScrolDy = std::clamp(autoScrolDy * 0.5f, -maxSpeed, maxSpeed);

		if (autoScrolDx != 0.f || autoScrolDy != 0.f)
		{
			// move the view (pixel deltas converted to doc coords)
			centerPos.x -= autoScrolDx / zoomFactor;
			centerPos.y -= autoScrolDy / zoomFactor;
			calcViewTransform(); // and redraws

			// pointer moves (relative to the view)
			int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
			onPointerMove(currentPointerPosAbsolute, flags);
		}

		return true;
	}

	void ViewBase::calcViewTransform()
	{
		// Quantize zoom so that every 12 DIPs maps to an integer number of physical pixels.
		constexpr float gridDips = 12.0f;
		const float dpiScale = drawingHost ? drawingHost->getRasterizationScale() : 1.0f;
		const float snappedGridPixels = std::round(zoomFactor * gridDips * dpiScale);
		const float snappedZoom = snappedGridPixels / (gridDips * dpiScale);

		const Point canvasCenter{ (drawingBounds.right - drawingBounds.left) * 0.5f, (drawingBounds.bottom - drawingBounds.top) * 0.5f };

		// Derive scroll offset from center (doc coords) and snapped zoom.
		float scrollX = canvasCenter.x - centerPos.x * snappedZoom;
		float scrollY = canvasCenter.y - centerPos.y * snappedZoom;

		// Snap scroll to physical pixels so grid lines land exactly on pixel boundaries.
		scrollX = std::round(scrollX * dpiScale) / dpiScale;
		scrollY = std::round(scrollY * dpiScale) / dpiScale;

		// Single transform used for both rendering and coordinate mapping.
		viewTransform = gmpi::drawing::makeScale({ snappedZoom, snappedZoom });
		viewTransform *= gmpi::drawing::makeTranslation({ scrollX, scrollY });

		viewTransformPrecise = viewTransform;
		inv_viewTransform = invert(viewTransformPrecise);

		invalidateRect();
	}

	void ViewBase::onHScroll(double visibleLeft)
	{
		if (avoidRecusion)
			return;

		const double viewWidth = drawingBounds.right - drawingBounds.left;
		centerPos.x = static_cast<float>(visibleLeft + viewWidth / (2.0 * zoomFactor));
		calcViewTransform();
	}

	void ViewBase::onVScroll(double visibleTop)
	{
		if (avoidRecusion)
			return;

		const double viewHeight = drawingBounds.bottom - drawingBounds.top;
		centerPos.y = static_cast<float>(visibleTop + viewHeight / (2.0 * zoomFactor));
		calcViewTransform();
	}

	void ViewBase::updateScrollBars()
	{
		if (!hscrollBar || !vscrollBar)
			return;

		avoidRecusion = true;

		constexpr double canvasSize = 7968.0;
		const double viewWidth  = drawingBounds.right  - drawingBounds.left;
		const double viewHeight = drawingBounds.bottom - drawingBounds.top;

		// All values in document coordinates.
		const double visibleWidth  = viewWidth  / zoomFactor;
		const double visibleHeight = viewHeight / zoomFactor;
		const double visibleLeft   = centerPos.x - visibleWidth  * 0.5;
		const double visibleTop    = centerPos.y - visibleHeight * 0.5;
		const double visibleRight  = visibleLeft + visibleWidth;
		const double visibleBottom = visibleTop  + visibleHeight;

		const double scrollMinX = (std::min)(visibleLeft,  0.0);
		const double scrollMinY = (std::min)(visibleTop,   0.0);
		const double canScrollH = (std::max)(0.0, canvasSize - visibleRight)  + (std::max)(0.0, visibleLeft);
		const double canScrollV = (std::max)(0.0, canvasSize - visibleBottom) + (std::max)(0.0, visibleTop);

		scrollBarSpec h;
		h.Value        = visibleLeft;
		h.Minimum      = scrollMinX;
		h.Maximum      = scrollMinX + canScrollH;
		h.ViewportSize = visibleWidth;
		h.LargeChange  = canScrollH * 0.2;
		h.SmallChange  = canScrollH * 0.05;
		hscrollBar(h);

		scrollBarSpec v;
		v.Value        = visibleTop;
		v.Minimum      = scrollMinY;
		v.Maximum      = scrollMinY + canScrollV;
		v.ViewportSize = visibleHeight;
		v.LargeChange  = canScrollV * 0.2;
		v.SmallChange  = canScrollV * 0.05;
		vscrollBar(v);

		avoidRecusion = false;
	}

	// dragStartPoint is the center of the pin we are dragging from, mousePoint is where the mouse is (usually very near)
	int32_t ViewBase::StartCableDrag(IViewChild* fromModule, int fromPin, Point dragStartPoint, gmpi::drawing::Point mousePoint)
	{
		auto fromPoint = dragStartPoint;

		ConnectorViewBase* cable = createCable(CableType::StructureCable, fromModule->getModuleHandle(), fromPin);

		cable->from_ = dragStartPoint;
		cable->to_ = mousePoint;
		cable->type = CableType::StructureCable;

		cable->pickup(1, mousePoint);

		assert(!isIteratingChildren);
		children.push_back(std::unique_ptr<IViewChild>(cable));

		int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_NEW | gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
		flags |= gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON;

		setCapture(cable);
		autoScrollStart();

		return (int) gmpi::ReturnCode::Ok;
	}

	bool ViewBase::OnCableMove(ConnectorViewBase* dragline)
	{
		// 4x drawn size is maximum snap distance.
		constexpr float maxSnapRangeSquared = 4 * sharedGraphicResources_struct::plugDiameter * sharedGraphicResources_struct::plugDiameter; // 4x drawn size is maximum snap distance.
		float bestDistanceSquared = maxSnapRangeSquared;

		ModuleView* bestModule{};
		int bestPinIndex = 0;
		for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
			(*it)->OnCableDrag(dragline, dragline->dragPoint(), bestDistanceSquared, bestModule, bestPinIndex);

		if (!bestModule)
			return false;

		auto pinLocation = bestModule->getConnectionPoint(dragline->type, bestPinIndex);
		pinLocation = bestModule->parent->MapPointToView(this, pinLocation);

		// snap line to pin.
		if(dragline->draggingFromEnd == 0)
			dragline->from_ = pinLocation;
		else
			dragline->to_ = pinLocation;

		return true;
	}

	bool ViewBase::EndCableDrag(gmpi::drawing::Point point, ConnectorViewBase* dragline, int32_t keyFlags)
	{
		Presenter()->OnCommand(PresenterCommand::CancelPickupLine);

		// no <ESC> key don't. assert(mouseCaptureObject != dragline); // caller should have released capture.
		if (mouseCaptureObject == dragline)
			releaseCapture();

//		const auto dragLineRect = dragline->getClipArea();
//		invalidateRect(&dragLineRect);
// needed?		ChildInvalidateRect(dragLineRect);

		if(dragline->type == CableType::StructureCable)
		{
			for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
			{
				if((*it)->EndCableDrag(point, dragline, keyFlags))
					return true; // connection made OK.
			}

			// unsuccessful, remove drag-line.
			RemoveChild(dragline); // WARNING children vector renewed, dragline no longer valid.
			return false;
		}
		else
		{
			const auto fromConnector = dragline->fmPin;
			const auto toConnector = dragline->toPin;

			gmpi::drawing::Point mousePos;
			int existingModuleHandle = -1;
			int existingModulePin = -1;

			if (dragline->draggingFromEnd == 0)
			{
				mousePos = dragline->from_;
				existingModuleHandle = toConnector.module;
				existingModulePin = toConnector.index;
			}
			else
			{
				mousePos = dragline->to_;
				existingModuleHandle = fromConnector.module;
				existingModulePin = fromConnector.index;
			}

			int newModuleHandle = -1;
			int newModulePin = -1;
			{
				constexpr float maxSnapRangeSquared = 4 * sharedGraphicResources_struct::plugDiameter * sharedGraphicResources_struct::plugDiameter; // 4x drawn size is maximum snap distance.
				float bestDistanceSquared = maxSnapRangeSquared;

				ModuleView* bestModule{};
				for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
					(*it)->OnCableDrag(dragline, dragline->dragPoint(), bestDistanceSquared, bestModule, newModulePin);

				if(bestModule)
					newModuleHandle = bestModule->getModuleHandle();
			}

			const bool wasConnected = fromConnector.module != -1 && toConnector.module != -1;

			if (newModuleHandle == -1) // didn't hit any pin.
			{
				if (wasConnected)
				{
					// remove the cable from the model. The view will refresh automatically, deleting the dragline.
					Presenter()->RemovePatchCable(
						fromConnector.module,
						fromConnector.index,
						toConnector.module,
						toConnector.index
					);
				}
				else
				{
					RemoveChild(dragline); // WARNING children vector renewed, pointer no longer valid.
				}
				return false;
			}

			// we did hit, was it back in the original pin?
			bool droppedBackInPlace{};
			if (dragline->draggingFromEnd == 0)
				droppedBackInPlace = newModuleHandle == fromConnector.module && newModulePin == fromConnector.index;
			else
				droppedBackInPlace = newModuleHandle == toConnector.module && newModulePin == toConnector.index;

			if (droppedBackInPlace) // then nothing changes.
			{
				dragline->draggingFromEnd = -1;
				return true;
			}

			int colorIndex = 0;
			if (auto patchcable = dynamic_cast<PatchCableView*>(dragline); patchcable)
				colorIndex = patchcable->getColorIndex();

			if (wasConnected)
			{
				// remove the cable from the model. The view will refresh automatically, deleting the dragline.
				Presenter()->RemovePatchCable(
					fromConnector.module,
					fromConnector.index,
					toConnector.module,
					toConnector.index
				);
			}
			else
			{
				// attempt to add new line may or may not refresh view, remove dragline to avoid dead-line in view if it doesn't.
				RemoveChild(dragline); // WARNING children vector renewed, pointer no longer valid.
			}
			dragline = {};

			// brand new connection.
			Presenter()->AddPatchCable(existingModuleHandle, existingModulePin, newModuleHandle, newModulePin, colorIndex, droppedBackInPlace);
		}
		return true;
	}

	void ViewBase::UpdateCablesBounds()
	{
		// update cables
		for(auto& c : children)
		{
			if(auto l = dynamic_cast<ConnectorViewBase*>(c.get()); l)
				l->OnModuleMoved();
		}
	}

	void ViewBase::OnPatchCablesUpdate(RawView patchCablesRaw)
	{
		// Remove old lines.
		assert(!isIteratingChildren);
		for(auto it = children.begin(); it != children.end(); )
		{
			auto l = dynamic_cast<PatchCableView*>((*it).get());
			if(l)
			{
				//				_RPT2(_CRT_WARN, "Ers Cable %x -> %x\n", l->fmPin.module, l->toPin.module);
				if (mouseOverObject == (*it).get())
					mouseOverObject = {};

				it = children.erase(it);
				continue;
			}

			++it;
		}

		const auto firstNewLine = children.size();

		assert(!isIteratingChildren);

		SE2::PatchCables cableList(patchCablesRaw);
		for(auto& c : cableList.cables)
		{
			auto module1 = Presenter()->HandleToObject(c.fromUgHandle);
			auto module2 = Presenter()->HandleToObject(c.toUgHandle);

			// avoid creating pointless PatchCableViews when the patch points are not visible.
			if (module1 && module2)
				children.push_back(std::make_unique<PatchCableView>(this, c.fromUgHandle, c.fromUgPin, c.toUgHandle, c.toUgPin, c.colorIndex));
		}

		for(auto i = firstNewLine; i < children.size(); ++i)
		{
			gmpi::drawing::Size desiredMax(0, 0);
			children[i]->measure(gmpi::drawing::Size(100000, 100000), &desiredMax);
			children[i]->arrange(gmpi::drawing::Rect(0, 0, desiredMax.width, desiredMax.height));
		}

		// may need to differentiate cables added *after* view opened with normal measure/arrange so they don't get measured/arranged twice or too soon etc.
		invalidateRect();
	}

	void ViewBase::RemoveCables(ConnectorViewBase* cable)
	{
		for(auto& child : children)
		{
			if(child.get() == cable)
			{
				Presenter()->RemovePatchCable(cable->fmPin.module, cable->fmPin.index, cable->toPin.module, cable->toPin.index);
				break;
			}
		}
	}

	std::pair<ConnectorViewBase*, int> ViewBase::getTopCable(int32_t handle, int32_t pinIdx)
	{
		for(auto& child : children)
		{
			auto cable = dynamic_cast<ConnectorViewBase*>(child.get());
			if(!cable)
				continue;

			auto whichEnd = cable->isConnectedToWhichEnd(handle, pinIdx);
			if(whichEnd >= 0)
				return {cable, whichEnd};
		}
		return {nullptr, -1};
	}

	// remove one module without invalidating entire view.
	void ViewBase::RemoveModule(int32_t handle)
	{
		if (mouseOverObject && mouseOverObject->getModuleHandle() == handle)
			mouseOverObject = nullptr;

		assert(!isIteratingChildren);

		auto it = std::find_if(children.begin(), children.end(), [handle](const std::unique_ptr<IViewChild>& child) {
			return child->getModuleHandle() == handle;
		});

		if (it != children.end())
		{
			auto& mod = *it;
			mod->preDelete();

			children.erase(it);
		}
	}

	void ViewBase::OnChangedChildHighlight(int phandle, int flags)
	{
		for(auto& m : children)
		{
			if(m->getModuleHandle() == phandle)
			{
				auto line = dynamic_cast<ConnectorViewBase*>(m.get());
				if(line)
					line->setHighlightFlags(flags);
				break;
			}
		}
	}

	void ViewBase::OnChildDspMessage(void* msg)
	{
		struct DspMsgInfo2
		{
			int id;
			int size;
			void* data;
			int handle;
		};

		const auto nfo = (DspMsgInfo2*)msg;

		if(auto m = Presenter()->HandleToObject(nfo->handle); m)
			m->receiveMessageFromAudio(msg);
	}

	void ViewBase::MoveToFront(IViewChild* child)
	{
		for(auto it = children.begin(); it != children.end(); ++it)
		{
			if (child == (*it).get())
			{
				auto c = std::move(*it);
				assert(!isIteratingChildren);
				it = children.erase(it);
				children.push_back(std::move(c));
				break;
			}
		}
	}

	void ViewBase::MoveToBack(IViewChild* child)
	{
		for(auto it = children.begin(); it != children.end(); ++it)
		{
			if (child == (*it).get())
			{
				assert(!isIteratingChildren);
				auto c = std::move(*it);
				it = children.erase(it);
				children.insert(children.begin(), std::move(c));
				break;
			}
		}
	}

	// Selected or dragged.
	void ViewBase::OnChangedChildSelected(int phandle, bool selected)
	{
		for(auto it = children.begin(); it != children.end(); ++it)
		{
			auto& m = *it;

			if(m->getModuleHandle() == phandle)
			{
				m->setSelected(selected);
				if(m->isVisable())
				{
					gmpi::drawing::Rect invalidRect;
					auto moduleview = dynamic_cast<ModuleView*>(m.get());
					if(selected && moduleview)
					{
						// Add resize adorner.
						std::unique_ptr<IViewChild> adorner = moduleview->createAdorner(this);

						gmpi::drawing::Size unused(0, 0);
						adorner->measure(gmpi::drawing::Size(0, 0), &unused); // provide for resizbility calc.
						invalidRect = adorner->getClipArea();
						assert(!isIteratingChildren);
						children.push_back(std::move(adorner));
					}
					else
					{
						invalidRect = m->getClipArea();
					}

					ChildInvalidateRect(invalidRect);

					// Avoid hitting Resize Adorner later in vector.
					if(selected)
						return;
				}

				if(!selected)
				{
					auto adorner = dynamic_cast<ResizeAdorner*>(m.get());
					if(adorner)
					{
//	needed?					auto r = m->getClipArea();
//						ChildInvalidateRect(&r);
                        
                        if (mouseOverObject == m.get())
                            mouseOverObject = {};

						assert(!isIteratingChildren);
						it = children.erase(it);

						return; // adorner should be last. no more to do.
					}
				}
			}
		}
	}

	void ViewBase::OnChangedChildPosition(int phandle, gmpi::drawing::Rect& newRect)
	{
		// Update module (and adorner position)
		bool needToUpdateCables = false;
		for(auto& m : children)
		{
			if(m->getModuleHandle() == phandle)
			{
				const auto originalRect = m->getLayoutRect();
				const gmpi::drawing::Size originalSize{ originalRect.right - originalRect.left, originalRect.bottom - originalRect.top };
				m->OnMoved(newRect);
				const auto movedRect = m->getLayoutRect();
				const gmpi::drawing::Size newSize{ movedRect.right - movedRect.left, movedRect.bottom - movedRect.top };

				// handle the case only of a container changing size becuase it's embedded sub-view changed siae (and we need to update any connected lines)
				needToUpdateCables |= originalSize != newSize;
			}
		}

		if(needToUpdateCables)
			UpdateCablesBounds();
	}

	void ViewBase::OnChangedChildNodes(int phandle, std::vector<gmpi::drawing::Point>& nodes)
	{
		for (auto& m : children)
		{
			if (m->getModuleHandle() == phandle)
			{
				m->OnNodesMoved(nodes);
			}
		}
	}

	// usefull for live reload of SEMs
	void ViewBase::Unload()
	{
		if(mouseCaptureObject || isDraggingModules)
			releaseCapture();

		// Clear out previous view.
		assert(!isIteratingChildren);
		children.clear();
		children_monodirectional.clear();
		isDraggingModules = false;
		patchAutomatorWrapper_ = nullptr;

		// Detach from the drawing frame to prevent use-after-free
		// when DSP parameter updates arrive after the editor window is closed.
		drawingHost = {};
		inputHost = {};

#if defined(_WIN32)
		frameWindow = nullptr;
#endif
	}

	void ViewBase::DragNewModule(const char* id)
	{
		if (id)
		{
			draggingNewModuleId = id;
			if (inputHost)
				inputHost->setCapture();
		}
		else // nullptr cancels drag
		{
			draggingNewModuleId.clear();
			if (inputHost)
				inputHost->releaseCapture();
			if (onDragNewModuleEnded)
				onDragNewModuleEnded();
		}
	}

	void ViewBase::Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap)
	{
		assert(guiObjectMap.empty());

		// Clear out previous view.
		assert(!isIteratingChildren);

		mouseOverObject = nullptr;
		isDraggingModules = false;
		patchAutomatorWrapper_ = nullptr;

		children.clear();

		if (mouseCaptureObject)
		{
			releaseCapture();
			mouseCaptureObject = {};
		}

#ifdef _DEBUG
//		debugInitializeCheck_ = false; // satisfy checks in base-class.
#endif

		//////////////////////////////////////////////

		BuildModules(context, guiObjectMap);
		BuildPatchCableNotifier(guiObjectMap);
		ConnectModules(*context, guiObjectMap);

		initMonoDirectionalModules(guiObjectMap);

		// remainder should mimic standard GUI module initialization.
		Presenter()->InitializeGuiObjects();
		initialize();

		gmpi::drawing::Size avail(drawingBounds.right - drawingBounds.left, drawingBounds.bottom - drawingBounds.top); // relying on frame to have set size already.
		gmpi::drawing::Size desired;
		measure(&avail, &desired);
		arrange(&drawingBounds);

		invalidateRect();

		if(!(*context)["draggingLineFromMod"].isNull() && !(*context)["draggingLineFromPin"].isNull())
		{
			const auto draggingLineFromMod = (*context)["draggingLineFromMod"].asInt();
			const auto draggingLineFromPin = (*context)["draggingLineFromPin"].asInt();
			const auto draggingLineToMod = (*context)["draggingLineToMod"].asInt();
			const auto draggingLineToPin = (*context)["draggingLineToPin"].asInt();

			if(draggingLineFromMod > -1)
			{
				auto fromView = Presenter()->HandleToObject(draggingLineFromMod);
				auto dragStartPoint = fromView->getConnectionPoint(CableType::StructureCable, draggingLineFromPin);

				// this is only an estimate of where the mouse is. Over the pin that it picked up the cable from.
				auto toView = Presenter()->HandleToObject(draggingLineToMod);
				auto mousePoint = toView->getConnectionPoint(CableType::StructureCable, draggingLineToPin);

				StartCableDrag(
					fromView,
					draggingLineFromPin,
					dragStartPoint,
					mousePoint
				);
			}
		}
	}

#if 0 // TODO support this
	int32_t ViewBase::populateContextMenu(float x, float y, gmpi::api::IUnknown* contextMenuItemsSink)
	{
		Presenter()->NotDragging();

		GmpiSdk::ContextMenuHelper menu(contextMenuCallbacks, contextMenuItemsSink);

		// Add items for Presenter
		// Cut, Copy, Paste etc.
		menu.currentCallback =
			[this](int32_t idx)
			{
				return Presenter()->onContextMenu(idx);
			};

		auto moduleHandle = mouseOverObject ? mouseOverObject->getModuleHandle() : -1; // -1 = no module under mouse.
		Presenter()->populateContextMenu(&menu, { x, y }, moduleHandle);

		if (mouseOverObject)
		{
//			menu.populateFromObject(x, y, mouseOverObject);

			menu.currentCallback =
				[this](int32_t idx)
				{
					return mouseOverObject->vc_onContextMenu(idx);
				};

			mouseOverObject->populateContextMenu(x, y, &menu);

		}
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode ViewBase::onContextMenu(int32_t idx)
	{
		return static_cast<gmpi::ReturnCode>(GmpiSdk::ContextMenuHelper::onContextMenu(contextMenuCallbacks, idx));
	}
#endif

	/*
	isArranging - Enables 3 pixel resize tolerance to cope with font size variation between GDI and DirectWrite. Not needed when user is dragging stuff.
	*/
	void ViewBase::OnChildResize(IViewChild* m)
	{
		if (m->isVisable() && dynamic_cast<ConnectorViewBase*>(m) == nullptr)
		{
			gmpi::drawing::Size savedSize(getWidth(m->getLayoutRect()), getHeight(m->getLayoutRect()));
			gmpi::drawing::Size desired;
			gmpi::drawing::Size actualSize;
			bool changedSize = false;
			/*
							if (debug)
							{
								_RPT4(_CRT_WARN, "savedSize r[ %f %f %f %f]\n", m->getBounds().left, m->getBounds().top, m->getBounds().left + m->getBounds().getWidth(), m->getBounds().top + m->getBounds().getHeight());
							}
			*/
			// Detect brand-new objects that haven't had size calculated yet.
			if (savedSize.width == 0 && savedSize.height == 0)
			{
				const float defaultDimensions = 100;
				gmpi::drawing::Size defaultSize(defaultDimensions, defaultDimensions);
				m->measure(defaultSize, &desired);
				actualSize = desired;
				// stick with integer sizes for compatibility.
				actualSize.height = ceilf(actualSize.height);
				actualSize.width = ceilf(actualSize.width);
				changedSize = true;
			}
			else
			{
#ifdef _DEBUG
				desired.width = std::numeric_limits<float>::quiet_NaN();
#endif

				m->measure(savedSize, &desired);

#ifdef _DEBUG
				assert(!std::isnan(desired.width)); // object does not implement measure()!
#endif
				/*
				if (debug)
				{
					_RPT2(_CRT_WARN, "desired s[ %f %f]\n", desired.width, desired.height);
				}
*/
// Font variations cause Slider to report different desired size.
// However resizing it causes alignment errors on Panel. It shifts left or right.
// Avoid resizing unless module clearly needs a different size. Structure view always sizes to fit (else plugs end up with wrapped text)
				float tolerence = getViewType() == CF_PANEL_VIEW ? 3.0f : 0.0f;
				if (isArranged || (fabsf(desired.width - savedSize.width) > tolerence || fabsf(desired.height - savedSize.height) > tolerence))
				{
					actualSize = desired;
					// stick with integer sizes for compatibility.
					actualSize.height = ceilf(actualSize.height);
					actualSize.width = ceilf(actualSize.width);
					changedSize = true;
				}
				else
				{
					// Used save size from project, even if it varies a little.
					actualSize = savedSize;
				}
			}

			if (changedSize)
			{
				gmpi::drawing::Rect newRect(m->getLayoutRect().left, m->getLayoutRect().top, m->getLayoutRect().left + actualSize.width, m->getLayoutRect().top + actualSize.height);
				m->OnMoved(newRect);
			}
		}
	}
	gmpi::ReturnCode ViewBase::measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize)
	{
		childrenDirty = true;
		processUnidirectionalModules(); // ensure images have loaded, otherwise measure will be immediatly invalidated.

		gmpi::drawing::Size veryLarge(10000, 10000);
		gmpi::drawing::Size notused;

		for (auto& c : children)
		{
			c->measure(veryLarge, &notused);
		}

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode ViewBase::arrange(const gmpi::drawing::Rect* finalRect)
	{
		drawingBounds = *finalRect;

		calcViewTransform(); // recompute scroll offset from center/zoom whenever view size changes

		// Modules first, then lines (which rely on module position being finalized).
		for (auto& m : children)
		{
			if (m->isVisable() && dynamic_cast<ConnectorViewBase*>(m.get()) == nullptr)
			{
				gmpi::drawing::Size savedSize(getWidth(m->getLayoutRect()), getHeight(m->getLayoutRect()));
				gmpi::drawing::Size desired;
				gmpi::drawing::Size actualSize;
				bool changedSize = false;
				/*
								if (debug)
								{
									_RPT4(_CRT_WARN, "savedSize r[ %f %f %f %f]\n", m->getBounds().left, m->getBounds().top, m->getBounds().left + m->getBounds().getWidth(), m->getBounds().top + m->getBounds().getHeight());
								}
				*/
				// Detect brand-new objects that haven't had size calculated yet.
				if (savedSize.width == 0 && savedSize.height == 0)
				{
					const float defaultDimensions = 100;
					gmpi::drawing::Size defaultSize(defaultDimensions, defaultDimensions);
					m->measure(defaultSize, &desired);
					actualSize = desired;
					// stick with integer sizes for compatibility.
					actualSize.height = ceilf(actualSize.height);
					actualSize.width = ceilf(actualSize.width);
					changedSize = true;
				}
				else
				{
#ifdef _DEBUG
					desired.width = std::numeric_limits<float>::quiet_NaN();
#endif

					m->measure(savedSize, &desired);

#ifdef _DEBUG
					assert(!std::isnan(desired.width)); // object does not implement measure()!
#endif
					/*
					if (debug)
					{
						_RPT2(_CRT_WARN, "desired s[ %f %f]\n", desired.width, desired.height);
					}
*/
// Font variations cause Slider to report different desired size.
// However resizing it causes alignment errors on Panel. It shifts left or right.
// Avoid resizing unless module clearly needs a different size. Structure view always sizes to fit (else plugs end up with wrapped text)
// Only during this during initial arrange, later when user drags object, use normal sizing logic.
					float tolerence = getViewType() == CF_PANEL_VIEW ? 3.0f : 0.0f;
					if (isArranged || (fabsf(desired.width - savedSize.width) > tolerence || fabsf(desired.height - savedSize.height) > tolerence))
					{
						actualSize = desired;
						// stick with integer sizes for compatibility.
						actualSize.height = ceilf(actualSize.height);
						actualSize.width = ceilf(actualSize.width);
						changedSize = true;
					}
					else
					{
						// Used save size from project, even if it varies a little.
						actualSize = savedSize;
					}
				}

				// Note, due to font width differences, this may result in different size/layout than original GDI graphics. e..g knobs shifting.
/* currently not used, also adorner could figure this out by itself
				gmpi::drawing::Size desiredMax(0, 0);
				m->measure(gmpi::drawing::Size(10000, 10000), &desiredMax);

				m->isResizableX = desired.width != desiredMax.width;
				m->isResizableY = desired.height != desiredMax.height;
*/
/*
				if (debug)
				{
					_RPT4(_CRT_WARN, "arrange r[ %f %f %f %f]\n", m->getBounds().left, m->getBounds().top, m->getBounds().left + actualSize.width, m->getBounds().top + actualSize.height);
				}
*/
				m->arrange(gmpi::drawing::Rect(m->getLayoutRect().left, m->getLayoutRect().top, m->getLayoutRect().left + actualSize.width, m->getLayoutRect().top + actualSize.height));

				// Typically only when new object inserted.
				if (changedSize) // actualSize != savedSize)
				{
					Presenter()->ResizeModule(m->getModuleHandle(), 2, 2, actualSize - savedSize);
				}
			}
		}

		for (auto& m : children)
		{
			if (dynamic_cast<ConnectorViewBase*>(m.get()))
				m->arrange(gmpi::drawing::Rect(0, 0, 10, 10));
		}

		isArranged = true;
		return gmpi::ReturnCode::Ok;
	}

	void ViewBase::OnPatchCablesVisibilityUpdate()
	{
		for (auto& o : children)
		{
			auto l = dynamic_cast<PatchCableView*>(o.get());
			if (l)
			{
				l->OnVisibilityUpdate();
			}
		}
	}

#if 0 // TODO implement
	void ViewBase::preGraphicsRedraw()
	{
		// Get any meter updates from DSP. ( See also CSynthEditAppBase::OnTimer() )
		Presenter()->GetPatchManager()->serviceGuiQueue();

		processUnidirectionalModules();
	}
#endif

	void ViewBase::markDirtyChild(IViewChild* child)
	{
		child->setDirty();
		childrenDirty = true;
	}

	/// //////////////////////////////////////////////////////////////////////////////////////////////////
	struct FeedbackTraceUi
	{
		std::list< std::pair<feedbackPinUi, feedbackPinUi> > feedbackConnectors;
		int reason_;

		FeedbackTraceUi(int reason) : reason_(reason) {}
		void AddLine(ModuleView* modA, ModuleView* modB, int pinA, int pinB)
		{
			feedbackConnectors.emplace_back(feedbackPinUi{ modA->getModuleHandle(), pinA, modA->name }, feedbackPinUi{ modB->getModuleHandle(), pinB, modB->name });
		}

		void DebugDump() {}
	};

	FeedbackTraceUi* CalcSortOrder3(ModuleView* mod, int& maxSortOrderGlobal)
	{
		if (mod->SortOrder >= 0) // Skip sorted modules.
			return {};

		mod->SortOrder = -2; // prevent recursion back to this.

		for (auto& c : mod->connections_)
		{
			const auto pinId = c.first;
			const bool isInput = std::find(mod->inputPinIds.begin(), mod->inputPinIds.end(), pinId) != mod->inputPinIds.end();
			if (!isInput)
				continue; // skip output pins.

			auto from = c.second.otherModule_;
			const int order = from->SortOrder;
			if (order == -2) // Found an feedback path, report it.
			{
				mod->SortOrder = -1; // Allow this to be re-sorted after feedback (potentially) compensated.
				auto e = new FeedbackTraceUi(0);// SE_FEEDBACK_PATH);
				e->AddLine(from, mod, c.second.otherModulePinIndex_, pinId);
				return e;
			}

			if (order == -1) // Found an unsorted path, go up it.
			{
				auto e = CalcSortOrder3(from, maxSortOrderGlobal);

				if (e) // Upstream module encountered feedback.
				{
					mod->SortOrder = -1; // Allow this to be re-sorted after feedback (potentially) compensated.

					// If downstream module has feedback, add trace information.
					e->AddLine(from, mod, c.second.otherModulePinIndex_, pinId);

					//int32_t handle{};
					//mod->getHandle(handle);
					const auto handle = mod->getModuleHandle();

					if (e->feedbackConnectors.front().second.moduleHandle == handle) // only reconstruct feedback loop as far as nesc.
					{
#if defined( _DEBUG )
						e->DebugDump();
#endif
						throw e;
					}
					return e;
				}
			}
		}

	done:

		mod->SortOrder = ++maxSortOrderGlobal;

#if 0 // _DEBUG
		_RPTN(_CRT_WARN, " SortOrder: %3d  ", SortOrder2);
		DebugIdentify();
		_RPT0(_CRT_WARN, "\n");
#endif

		return {};
	}

	FeedbackTraceUi* SortOrderSetup3(std::vector<ModuleView*> children, int& maxSortOrderGlobal)
	{
		for (auto& ug : children)
		{
			if (ug->SortOrder != -1) // already sorted?
				continue;

			// skip intermediate modules, since we'll be sorting them from downstream anyhow.
			// this may include being sorted by an outer container later (i.e. not here).
			const bool hasOutputLines = !ug->connections_.empty();
			if (hasOutputLines)
				continue;

			// recurse upstream as far as possible, assigning sortorder
			auto e = CalcSortOrder3(ug, maxSortOrderGlobal);

			if (e)
			{
#if defined( _DEBUG )
				e->DebugDump();
#endif

				return e;
			}
		}

		// Rare case. when patch consists only of modules feeding back into each other (no 'downstream' module at all)
		for (auto& ug : children)
		{
			if (ug->SortOrder != -1) // already sorted?
				continue;

			// recurse upstream as far as possible, assigning sortorder
			auto e = CalcSortOrder3(ug, maxSortOrderGlobal);

			if (e)
			{
#if defined( _DEBUG )
				e->DebugDump();
#endif

				return e;
			}
		}

		return {};
	}

	void ViewBase::initMonoDirectionalModules(std::map<int, SE2::ModuleView*>& guiObjectMap)
	{
		Presenter()->ClearFeedbackHighlights();

		children_monodirectional.clear();

		// pull out all mono-directional modules.
		for (auto& m : guiObjectMap)
		{
			auto module = m.second;
			if (module->isMonoDirectional())
			{
				children_monodirectional.push_back(dynamic_cast<ModuleView*>(module));
			}
		}

		int maxSortOrderGlobal = -1;
		auto e = SortOrderSetup3(children_monodirectional, maxSortOrderGlobal);

		if (e)
		{
			_RPT0(_CRT_WARN, "ERROR: Feedback loop detected in module graph.\n");
			Presenter()->HighlightFeedback(e->feedbackConnectors);
		}

		std::sort(children_monodirectional.begin(), children_monodirectional.end(), [](const auto& a, const auto& b)
			{
				return a->SortOrder < b->SortOrder;
			});

#if 0 //def _DEBUG
		for (auto& ug : children_monodirectional)
		{
			_RPTN(_CRT_WARN, "%3d : %s\n", ug->SortOrder, ug->name.c_str());
		}
#endif
	}

	void ViewBase::processUnidirectionalModules()
	{
		if (!childrenDirty)
			return;

		childrenDirty = false;

		for (auto& m : children_monodirectional)
		{
			if (m->getDirty())
				m->process();
		}
	}

	int32_t ViewBase::OnKeyPress(wchar_t c) // SDK3 version, forward to new version.
	{
		return static_cast<int32_t>(onKeyPress(c));
	}

	gmpi::ReturnCode ViewBase::getDrawingFactory(gmpi::api::IUnknown** returnFactory)
	{
		if (!drawingHost)
			return gmpi::ReturnCode::NoSupport;

		return drawingHost->getDrawingFactory(returnFactory);
	}

	gmpi::ReturnCode ViewBase::onKey(int32_t key, gmpi::drawing::Point* pointerPosOrNull)
	{
		switch (key)
		{
		case 0x25: // left
		case 0x27: // right
		case 0x26: // up
		case 0x28: // down
			break;

		default:
			Presenter()->NotDragging();
			break;
		}

		switch (key)
		{
		case 0x25: // left
			Presenter()->DragSelection({ -1, 0 });
			break;

		case 0x27: // right
			Presenter()->DragSelection({ 1, 0 });
			break;

		case 0x26: // up
			Presenter()->DragSelection({ 0, -1 });
			break;

		case 0x28: // down
			Presenter()->DragSelection({ 0, 1 });
			break;

		case 0x1B: // <ESC> to cancel cable or module drag
			if (auto cable = dynamic_cast<SE2::ConnectorViewBase*>(mouseCaptureObject); cable)
			{
				autoScrollStop();
				EndCableDrag({ -10000, -10000 }, cable, 0);
				return gmpi::ReturnCode::Handled;
			}
			else if (!draggingNewModuleId.empty())
			{
				DragNewModule(nullptr);
				return gmpi::ReturnCode::Handled;
			}
			break;

		case 'n': // new module picker
		case 'N':
			//if (pointerPosOrNull)
			//{
			//	if (DoModulePicker(*pointerPosOrNull))
			//		return gmpi::ReturnCode::Handled;
			//}
			break;

		default:
			break;
		}

		return gmpi::ReturnCode::Unhandled;
	}

	bool ViewBase::DoModulePicker(gmpi::drawing::Point currentPointerPos)
	{
		if(modulePicker)
			return false;

		modulePicker = Presenter()->createModulePicker(this);

		if (!modulePicker) // only in edit mode
			return false;

		assert(!isIteratingChildren);
		children.push_back(std::unique_ptr<IViewChild>(modulePicker));

		gmpi::drawing::Size desiredMax{};
		modulePicker->measure(gmpi::drawing::Size(100, 80), &desiredMax);
		modulePicker->arrange(gmpi::drawing::Rect(currentPointerPos.x, currentPointerPos.y, currentPointerPos.x + desiredMax.width, currentPointerPos.y + desiredMax.height));

//		modulePicker->init();

		const auto invalidRect = modulePicker->getClipArea();
		ChildInvalidateRect(invalidRect);
		return true;
	}

	void ViewBase::DismissModulePicker()
	{
		const auto invalidRect = modulePicker->getClipArea();
		RemoveChild(modulePicker);
		modulePicker = nullptr;
		ChildInvalidateRect(invalidRect);
	}

	IViewChild* ViewBase::Find(gmpi::drawing::Point& p)
	{
		for (auto it = children.rbegin(); it != children.rend(); ++it)
		{
			auto m = (*it).get();
			if (m)
			{
				auto b = m->getLayoutRect();
				if (p.y < b.bottom && p.y >= b.top && p.x < b.right && p.x >= b.left)
				{
					return m;
				}
			}
		}

		return nullptr;
	}
}// namespace
