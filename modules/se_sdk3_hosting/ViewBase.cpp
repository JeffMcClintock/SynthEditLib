
#include <random>
#include <sstream>
#include <iostream>
#include "ViewBase.h"
#include "ConnectorView.h"
#include "modules/se_sdk3_hosting/Presenter.h"
#include "ResizeAdorner.h"
#include "GuiPatchAutomator3.h"
#include "UgDatabase.h"
#include "modules/shared/unicode_conversion.h"
#include "RawConversions.h"
#include "SubViewPanel.h"
#include "DragLine.h"
#include "modules/shared/xplatform_modifier_keys.h"
#include "IPluginGui.h"
#include "mfc_emulation.h"
#include "IGuiHost2.h"
#include "helpers/Timer.h"

#ifdef _WIN32
#include "Shared/DrawingFrame2_win.h"
#endif
// #define DEBUG_HIT_TEST

using namespace std;
using namespace gmpi;
using namespace gmpi_gui;
using namespace GmpiDrawing;
using namespace GmpiGuiHosting;

namespace SE2
{
	ViewBase::ViewBase(GmpiDrawing::Size size) :
		drawingBounds{ 0, 0, size.width, size.height }
	{
	}

	void ViewBase::setDocument(SE2::IPresenter* ppresentor)
	{
		Init(ppresentor);
	}

	int32_t ViewBase::setHost(gmpi::IMpUnknown* host)
	{
#if defined(_WIN32)
		frameWindow = dynamic_cast<DrawingFrameBase2*>(host);
#endif
		return gmpi_gui::MpGuiGfxBase::setHost(host);
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
										{
											dt = DT_INT;
										}

										auto raw = ParseToRaw(dt, default_element.asString());

										wrapper->setPin(0, 0, pinId, 0, (int32_t)raw.size(), (void*)(&raw[0]));

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

	int32_t ViewBase::OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext)
	{
		Graphics g(drawingContext);

		// Restrict drawing only to overall clip-rect.
		auto cliprect = g.GetAxisAlignedClip();
		//_RPT4(_CRT_WARN, "OnRender    clip[ %d %d %d %d]\n", (int)cliprect.left, (int)cliprect.top, (int)cliprect.right, (int)cliprect.bottom);

		const Matrix3x2 originalTransform = g.GetTransform();

		for(auto& m : children)
		{
			auto b = m->GetClipRect();
			if(isOverlapped(b, cliprect))
			{
				if(dynamic_cast<ConnectorViewBase*>(m.get()))
				{
					g.SetTransform(originalTransform);
				}
				else
				{
					auto layoutRect = m->getLayoutRect();
					auto adjustedTransform = Matrix3x2::Translation(layoutRect.left, layoutRect.top) * originalTransform;
					g.SetTransform(adjustedTransform);
				}

				m->OnRender(g);
				//				_RPT0(_CRT_WARN, "X");
			}
		}

		g.SetTransform(originalTransform);
		//		_RPT0(_CRT_WARN, "\n");

		return gmpi::MP_OK;
	}

	int32_t ViewBase::setCapture(IViewChild* module)
	{
		//		assert(dynamic_cast<SE2::PatchCableView*>(module) == 0);
		mouseCaptureObject = module;
		return getGuiHost()->setCapture();
	}

	int32_t ViewBase::releaseCapture()
	{
		mouseCaptureObject = nullptr;
		auto r = getGuiHost()->releaseCapture();

		calcMouseOverObject(0);

		return r;
	}

	int32_t ViewBase::getToolTip(MP1_POINT point, gmpi::IString* returnString)
	{
		std::string returnToolTip;

		for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
		{
			auto& m = *it;
			if(m->hitTest(0, point))
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
		return gmpi::MP_OK;
	}

	// #define DEBUG_HIT_TEST 1

	int32_t ViewBase::onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point)
	{
#ifdef DEBUG_HIT_TEST
		_RPT3(0, "ViewBase::onPointerDown(%x, (%f, %f))\n", flags, point.x, point.y);
#endif
		Presenter()->NotDragging();

		// handle edge-case of mouse clicking without any prior 'OnMove' (e.g. after clicking to make a pop-up menu disappear).
		// ensures that 'mouseOverObject' is correct.
		if (lastMovePoint.x != point.x || lastMovePoint.y != point.y)
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

			onPointerMove(simulatedFlags, point);
		}

		if(mouseCaptureObject)
		{
#ifdef DEBUG_HIT_TEST
			_RPT1(0, "mouseCaptureObject=%x\n", mouseCaptureObject);
#endif

			return mouseCaptureObject->onPointerDown(flags, point);
		}

		// account for objects appearing without mouse moving (e.g. show-on-parent changing on previous click).
		calcMouseOverObject(flags);

		IViewChild* hitObject = nullptr;
		if(mouseOverObject)
		{
			auto result = mouseOverObject->onPointerDown(flags, point);

			// Module has captured mouse. Let it take over.
			if( /*result == gmpi::MP_OK ||*/ mouseCaptureObject)
			{
#ifdef DEBUG_HIT_TEST
				_RPT0(0, " and captured\n");
#endif
				return gmpi::MP_OK;
			}

			if(result == gmpi::MP_OK) // result == gmpi::MP_OK indicates mouse 'hit'
			{
				hitObject = mouseOverObject;
			}

			if(result == gmpi::MP_HANDLED) // indicates mouse 'hit' AND handled already.
			{
#ifdef DEBUG_HIT_TEST
				_RPT0(0, " and not captured\n");
#endif
				return result; // no further handling needed.
			}
#ifdef DEBUG_HIT_TEST
			_RPT0(0, "\n");
#endif
		}

		if (modulePicker && hitObject != modulePicker)
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

				getGuiHost()->setCapture();
				autoScrollStart();
			}
		}
		else
		{
#ifdef DEBUG_HIT_TEST
			_RPT0(0, "Nothing hit \n");
#endif

			// Nothing hit, clear selection (left click only).
			if ((flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0)
			{
				Presenter()->ObjectClicked(-1, flags); //gmpi::modifier_keys::getHeldKeys());
			}

			if(Presenter()->editEnabled())
			{
				if((flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0) // Drag selection box.
				{
					assert(!isIteratingChildren);
					children.push_back(std::unique_ptr<IViewChild>(new SelectionDragBox(this, point)));
					autoScrollStart();
					return gmpi::MP_OK;
				}
			}
		}

		// indicate successful hit.
		return hitObject ? gmpi::MP_OK : gmpi::MP_UNHANDLED;
	}

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

	void ViewBase::OnDragSelectionBox(int32_t flags, GmpiDrawing::Rect selectionRect)
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

	int32_t ViewBase::setHover(bool isMouseOverMe)
	{
		if (!isMouseOverMe && mouseOverObject)
		{
			mouseOverObject->setHover(false);
			mouseOverObject = {};

			return gmpi::MP_OK;
		}

		return gmpi::MP_UNHANDLED;
	}

	int32_t ViewBase::onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point)
	{
		lastMovePoint = point;

		if(mouseCaptureObject)
		{
			mouseCaptureObject->onPointerMove(flags, point);
		}
		else
		{
			if (isDraggingModules) // could this be handled with custom mouseCaptureObject? to remove need for check here?
			{
				// Snap-to-grid logic.
				GmpiDrawing::Size delta(point.x - pointPrev.x, point.y - pointPrev.y);
				if (delta.width != 0.0f || delta.height != 0.0f) // avoid false snap on selection
				{
					const auto snapGridSize = Presenter()->GetSnapSize();

					GmpiDrawing::Point dragModuleTopLeft = DraggingModulesInitialTopLeft + DraggingModulesOffset;
					GmpiDrawing::Point newPoint = dragModuleTopLeft + delta;

					newPoint.x = floorf((snapGridSize / 2 + newPoint.x) / snapGridSize) * snapGridSize;
					newPoint.y = floorf((snapGridSize / 2 + newPoint.y) / snapGridSize) * snapGridSize;
					GmpiDrawing::Size snapDelta = newPoint - dragModuleTopLeft;

					pointPrev += snapDelta;

					if (snapDelta.width != 0.0 || snapDelta.height != 0.0)
					{
						Presenter()->DragSelection(snapDelta);
						DraggingModulesOffset += snapDelta;
					}
				}
				return gmpi::MP_OK;
			}
		}

		calcMouseOverObject(flags);

		if(mouseOverObject)
		{
			mouseOverObject->onPointerMove(flags, point);
		}

		return gmpi::MP_OK;
	}

	void ViewBase::onSubPanelMadeVisible()
	{
		// if a sub-panel just blinked into existence, need to update mouse over object on myself AND on it. Else the next click will be ignored.
		// calling ViewBase to avoid the offset imposed by the sub-panel (which has already been accounted for)
		ViewBase::onPointerMove(0, lastMovePoint);
	}

	void ViewBase::calcMouseOverObject(int32_t flags)
	{
		// when one object has captured mouse, don't highlight other objects.
		if (mouseCaptureObject)
			return;

		IViewChild* hitObject{};

		isIteratingChildren = true;
		for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
		{
			auto& m = *it;
			if(m->hitTest(flags, lastMovePoint))
			{
				hitObject = m.get();
				break;
			}
		}
		isIteratingChildren = false;

		if(hitObject != mouseOverObject)
		{
			if(mouseOverObject)
			{
				mouseOverObject->setHover(false);
			}

			mouseOverObject = hitObject;

			if(mouseOverObject)
			{
				mouseOverObject->setHover(true);
			}
		}
	}

	void ViewBase::OnChildDeleted(IViewChild* childObject)
	{
		if (mouseOverObject == childObject)
		{
			mouseOverObject = nullptr;
		}
	}

	int32_t ViewBase::onMouseWheel(int32_t flags, int32_t delta, GmpiDrawing_API::MP1_POINT point)
	{
		if (isDraggingModules)
			return gmpi::MP_UNHANDLED;

		calcMouseOverObject(flags);

		if (!mouseOverObject)
			return gmpi::MP_UNHANDLED;

		return mouseOverObject->onMouseWheel(flags, delta, point);
	}

	int32_t ViewBase::StartCableDrag(IViewChild* fromModule, int fromPin, Point dragStartPoint, bool isHeldAlt, CableType type)
	{
		auto fromPoint = dragStartPoint;

		// Check for existing cables, long click grabs? or shift-click?
		if(isHeldAlt)
		{
			/*
			for(auto it = children.rbegin(); it != children.rend(); )
			{
				auto l = dynamic_cast<PatchCableView*>((*it).get());
				if(l)
				{
					if(l->fromModuleHandle() == fromModule->getModuleHandle() && l->fromPin() == fromPin)
					{
						l->pickup(0, fromPoint);
						break;
					}
					if(l->toModuleHandle() == fromModule->getModuleHandle() && l->toPin() == fromPin)
					{
						l->pickup(1, fromPoint);
						break;
					}
				}

				++it;
			}
			*/
		}
		else
		{
			// Not <ALT> held.

			ConnectorViewBase* cable = createCable(type, fromModule->getModuleHandle(), fromPin);

			cable->from_ = fromPoint;
			cable->pickup(1, fromPoint);
			cable->type = type;

			assert(!isIteratingChildren);
			children.push_back(std::unique_ptr<IViewChild>(cable));

			int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_NEW | gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
			flags |= gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON;

			setCapture(cable);
			autoScrollStart();
		}

		return gmpi::MP_OK;
	}

	void ViewBase::OnCableMove(ConnectorViewBase* dragline)
	{
		float bestDistanceSquared = 2 * sharedGraphicResources_struct::plugDiameter; // 4x drawn size is maximum snap distance.
		bestDistanceSquared *= bestDistanceSquared;

		IViewChild* bestModule = nullptr;
		int bestPinIndex = 0;
		for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
		{
			(*it)->OnCableDrag(dragline, dragline->dragPoint(), bestDistanceSquared, bestModule, bestPinIndex);
		}

		if(bestModule)
		{
			// snap line to pin.
			if(dragline->draggingFromEnd == 0)
			{
				dragline->from_ = bestModule->getConnectionPoint(dragline->type, bestPinIndex);
				dragline->from_ = dynamic_cast<ModuleView*>(bestModule)->parent->MapPointToView(this, dragline->from_);
			}
			else
			{
				dragline->to_ = bestModule->getConnectionPoint(dragline->type, bestPinIndex);
				dragline->to_ = dynamic_cast<ModuleView*>(bestModule)->parent->MapPointToView(this, dragline->to_);
			}
		}
	}

	// moving an existing cable
	int32_t ViewBase::EndCableDrag(GmpiDrawing_API::MP1_POINT point, ConnectorViewBase* dragline)
	{
		bool presenterGonnaRefresh = false;
		
		if(dragline->type == CableType::StructureCable)
		{
			for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
			{
				if((*it)->EndCableDrag(point, dragline))
				{
					presenterGonnaRefresh = true;
					break;
				}
			}
		}
		else
		{
			GmpiDrawing::Point mousePos;
			int existingModuleHandle = -1;
			int existingModulePin = -1;

			if(dragline->draggingFromEnd == 0)
			{
				mousePos = dragline->from_;
				existingModuleHandle = dragline->toModuleHandle();
				existingModulePin = dragline->toPin();
			}
			else
			{
				mousePos = dragline->to_;
				existingModuleHandle = dragline->fromModuleHandle();
				existingModulePin = dragline->fromPin();
			}

			int newModuleHandle = -1;
			int newModulePin = -1;

			{
				float bestDistanceSquared = 2 * sharedGraphicResources_struct::plugDiameter; // 4x drawn size is maximum snap distance.
				bestDistanceSquared *= bestDistanceSquared;

				IViewChild* bestModule = nullptr;
				for(auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
				{
					(*it)->OnCableDrag(dragline, dragline->dragPoint(), bestDistanceSquared, bestModule, newModulePin);
				}

				if(bestModule)
					newModuleHandle = bestModule->getModuleHandle();
			}
			
			// dragline to be deleted, save nesc info.
			const auto fromModuleHandle = dragline->fromModuleHandle();
			const auto fromModulePin = dragline->fromPin();
			const auto toModuleHandle = dragline->toModuleHandle();
			const auto toModulePin = dragline->toPin();
			const auto draggingFromEnd = dragline->draggingFromEnd;
			int colorIndex = 0;
			if (auto patchcable = dynamic_cast<PatchCableView*>(dragline); patchcable)
			{
				colorIndex = patchcable->getColorIndex();
			}

			// In the case of dragging the end of an existing cable, erase old route.
			Presenter()->RemovePatchCable(fromModuleHandle, fromModulePin, toModuleHandle, toModulePin);

			if(newModuleHandle != -1)
			{
				bool droppedBackInPlace;
				if(draggingFromEnd == 0)
				{
					droppedBackInPlace = newModuleHandle == fromModuleHandle && newModulePin == fromModulePin;
				}
				else
				{
					droppedBackInPlace = newModuleHandle == toModuleHandle && newModulePin == toModulePin;
				}

				if(existingModuleHandle >= 0)
				{
					presenterGonnaRefresh = Presenter()->AddPatchCable(existingModuleHandle, existingModulePin, newModuleHandle, newModulePin, colorIndex, droppedBackInPlace);
				}
			}

			if (!presenterGonnaRefresh)
				invalidateRect();
		}

		// avoid erasing the drawline immediatly if it's going to result in a new line anyhow (to avoid jarring wait for new line to appear).
		// we're relying on a complete refresh to discard the temporary drag-line
		if (!presenterGonnaRefresh)
		{
			// Remove drag line.
			for (auto it = children.begin(); it != children.end(); ++it)
			{
				if (dragline == (*it).get())
				{
					if (mouseCaptureObject == dragline)
					{
						releaseCapture();
					}
					if (mouseOverObject == dragline)
						mouseOverObject = {};

					const auto dragLineRect = (*it)->GetClipRect();
					invalidateRect(&dragLineRect);

					assert(!isIteratingChildren);
					it = children.erase(it);
					break;
				}
			}
		}

		return gmpi::MP_OK;
	}

	void ViewBase::UpdateCablesBounds()
	{
		// update cables
		for(auto& c : children)
		{
			auto l = dynamic_cast<ConnectorViewBase*>(c.get());
			if(l)
			{
				l->OnModuleMoved();
			}
		}
	}

	void ViewBase::OnPatchCablesUpdate(RawView patchCablesRaw)
	{
		// Remove old lines.
		for(auto it = children.begin(); it != children.end(); )
		{
			auto l = dynamic_cast<PatchCableView*>((*it).get());
			if(l)
			{
				//				_RPT2(_CRT_WARN, "Ers Cable %x -> %x\n", l->fromModuleHandle(), l->toModuleHandle());
				if (mouseOverObject == (*it).get())
				{
					mouseOverObject = {};
				}
				assert(!isIteratingChildren);
				it = children.erase(it);
				continue;
			}

			++it;
		}

		const auto firstNewLine = children.size();

		SE2::PatchCables cableList(patchCablesRaw);
		for(auto& c : cableList.cables)
		{
			//			_RPT2(_CRT_WARN, "New Cable %x -> %x\n", c.fromUgHandle, c.toUgHandle);
			assert(!isIteratingChildren);
			children.push_back(std::make_unique<PatchCableView>(this, c.fromUgHandle, c.fromUgPin, c.toUgHandle, c.toUgPin, c.colorIndex));
		}

		for(auto i = firstNewLine; i < children.size(); ++i)
		{
			GmpiDrawing::Size desiredMax(0, 0);
			children[i]->measure(GmpiDrawing::Size(100000, 100000), &desiredMax);
			children[i]->arrange(GmpiDrawing::Rect(0, 0, desiredMax.width, desiredMax.height));
		}

		// may need to differentiate cables added *after* view opened with normal measure/arrange so they don't get measured/arranged twice or too soon etc.
		invalidateRect();
	}

	void ViewBase::RemoveCables(ConnectorViewBase* cable)
	{
		for(auto it = children.begin(); it != children.end(); ++it)
		{
			if((*it).get() == cable)
			{
				Presenter()->RemovePatchCable(cable->fromModuleHandle(), cable->fromPin(), cable->toModuleHandle(), cable->toPin());
				break;
			}
		}
	}

	void ViewBase::RemoveModule(int32_t handle)
	{
		if (mouseOverObject && mouseOverObject->getModuleHandle() == handle)
		{
			mouseOverObject = nullptr;
		}

		assert(!isIteratingChildren);

		std::erase_if( children,
			[handle](const std::unique_ptr<IViewChild>& child) -> bool
			{
				return child->getModuleHandle() == handle;
			}
		);

	}

	void ViewBase::OnChangedChildHighlight(int phandle, int flags)
	{
		for(auto& m : children)
		{
			if(m->getModuleHandle() == phandle)
			{
				auto line = dynamic_cast<ConnectorViewBase*>(m.get());
				if(line)
				{
					line->setHighlightFlags(flags);
				}
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

		auto m = Presenter()->HandleToObject(nfo->handle);
		if(m)
		{
			m->receiveMessageFromAudio(msg);
		}
		/*
				for (auto& m : children)
				{
					if (m->getModuleHandle() == nfo->handle)
					{
						m->receiveMessageFromAudio(msg);
					}
				}
		*/
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
					GmpiDrawing_API::MP1_RECT invalidRect;
					auto moduleview = dynamic_cast<ModuleView*>(m.get());
					if(selected && moduleview)
					{
						// Add resize adorner.
						std::unique_ptr<IViewChild> adorner = moduleview->createAdorner(this);

						GmpiDrawing::Size unused(0, 0);
						adorner->measure(GmpiDrawing::Size(0, 0), &unused); // provide for resizbility calc.
						invalidRect = adorner->getLayoutRect();
						assert(!isIteratingChildren);
						children.push_back(std::move(adorner));
					}
					else
					{
						invalidRect = m->GetClipRect();
					}

					getGuiHost()->invalidateRect(&invalidRect);

					// Avoid hitting Resize Adorner later in vector.
					if(selected)
						return;
				}

				if(!selected)
				{
					auto adorner = dynamic_cast<ResizeAdorner*>(m.get());
					if(adorner)
					{
						auto r = m->GetClipRect();
						invalidateRect(&r);

						assert(!isIteratingChildren);
						it = children.erase(it);
						if (mouseOverObject == m.get())
						{
							mouseOverObject = {};
						}
						return; // adorner should be last. no more to do.
					}
				}
			}
		}
	}

	void ViewBase::OnChangedChildPosition(int phandle, GmpiDrawing::Rect& newRect)
	{
		// Update module (and adorner position)
		bool needToUpdateCables = false;
		for(auto& m : children)
		{
			if(m->getModuleHandle() == phandle)
			{
				const auto originalSize = m->getLayoutRect().getSize();
				m->OnMoved(newRect);
				const auto newSize = m->getLayoutRect().getSize();

				// handle the case only of a container changing size becuase it's embedded sub-view changed siae (and we need to update any connected lines)
				needToUpdateCables |= originalSize != newSize;
			}
		}

		if(needToUpdateCables)
			UpdateCablesBounds();
	}

	void ViewBase::OnChangedChildNodes(int phandle, std::vector<GmpiDrawing::Point>& nodes)
	{
		for (auto& m : children)
		{
			if (m->getModuleHandle() == phandle)
			{
				m->OnNodesMoved(nodes);
			}
		}
	}

	void ViewBase::autoScrollStart()
	{
#if defined (_WIN32)
		if(frameWindow)
			frameWindow->autoScrollStart();
#endif
	}

	void ViewBase::autoScrollStop()
	{
#if defined (_WIN32)
		if (frameWindow)
			frameWindow->autoScrollStop();
#endif
	}

	// usefull for live reload of SEMs
	void ViewBase::Unload()
	{
		if(mouseCaptureObject || isDraggingModules)
			releaseCapture();

		// Clear out previous view.
		assert(!isIteratingChildren);
		children.clear();
		isDraggingModules = false;
		patchAutomatorWrapper_ = nullptr;
	}

	void ViewBase::DragNewModule(const char* id)
	{
		if (id)
		{
			draggingNewModuleId = id;
			getGuiHost()->setCapture();
		}
		else // nullptr cancels drag
		{
			draggingNewModuleId.clear();
			getGuiHost()->releaseCapture();
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
		debugInitializeCheck_ = false; // satisfy checks in base-class.
#endif

		//////////////////////////////////////////////

		BuildModules(context, guiObjectMap);
		BuildPatchCableNotifier(guiObjectMap);
		ConnectModules(*context, guiObjectMap);

		initMonoDirectionalModules(guiObjectMap);

		// remainder should mimic standard GUI module initialization.
		Presenter()->InitializeGuiObjects();
		initialize();

		GmpiDrawing::Size avail(drawingBounds.getWidth(), drawingBounds.getHeight()); // relying on frame to have set size already.
		GmpiDrawing::Size desired;
		measure(avail, &desired);
		arrange(drawingBounds);

		invalidateRect();
	}

	int32_t ViewBase::populateContextMenu(float x, float y, gmpi::IMpUnknown* contextMenuItemsSink)
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
			menu.populateFromObject(x, y, mouseOverObject);
		}
		return gmpi::MP_OK;
	}

	int32_t ViewBase::onContextMenu(int32_t idx)
	{
		return GmpiSdk::ContextMenuHelper::onContextMenu(contextMenuCallbacks, idx);
	}

	int32_t ViewBase::onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point)
	{
		Presenter()->NotDragging();

		if (!draggingNewModuleId.empty())
		{
			int32_t isMouseCaptured{};
			getGuiHost()->getCapture(isMouseCaptured);
			if (isMouseCaptured)
			{
				getGuiHost()->releaseCapture();
				const auto moduleId = Utf8ToWstring(draggingNewModuleId); // TODO why does this get converted to UTF8 then back again?
				Presenter()->AddModule(moduleId.c_str(), point);
			}
			draggingNewModuleId.clear();
		}

		if (mouseCaptureObject)
		{
			mouseCaptureObject->onPointerUp(flags, point);
		}

#ifdef _DEBUG
		if (mouseCaptureObject)
		{
			_RPT0(_CRT_WARN, "WARNING: GUI MODULE DID NOT RELEASE MOUSECAPTURE!!!\n");
		}
#endif

		if (isDraggingModules)
		{
			isDraggingModules = false;
			releaseCapture();
			autoScrollStop();
		}

		return gmpi::MP_OK;
	}

	/*
	isArranging - Enables 3 pixel resize tolerance to cope with font size variation between GDI and DirectWrite. Not needed when user is dragging stuff.
	*/
	void ViewBase::OnChildResize(IViewChild* m)
	{
		if (m->isVisable() && dynamic_cast<ConnectorViewBase*>(m) == nullptr)
		{
			GmpiDrawing::Size savedSize(m->getLayoutRect().getWidth(), m->getLayoutRect().getHeight());
			GmpiDrawing::Size desired;
			GmpiDrawing::Size actualSize;
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
				GmpiDrawing::Size defaultSize(defaultDimensions, defaultDimensions);
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
				GmpiDrawing::Rect newRect(m->getLayoutRect().left, m->getLayoutRect().top, m->getLayoutRect().left + actualSize.width, m->getLayoutRect().top + actualSize.height);
				m->OnMoved(newRect);
			}
		}
	}

	int32_t ViewBase::measure(MP1_SIZE availableSize, MP1_SIZE* returnDesiredSize)
	{
		childrenDirty = true;
		processUnidirectionalModules(); // ensure images have loaded, otherwise measure will be immediatly invalidated.

		GmpiDrawing::Size veryLarge(10000, 10000);
		GmpiDrawing::Size notused;

		for (auto& c : children)
		{
			c->measure(veryLarge, &notused);
		}

		return gmpi::MP_OK;
	}

	int32_t ViewBase::arrange(MP1_RECT finalRect)
	{
		drawingBounds = finalRect;

		// Modules first, then lines (which rely on module position being finalized).
		for (auto& m : children)
		{
			if (m->isVisable() && dynamic_cast<ConnectorViewBase*>(m.get()) == nullptr)
			{
				GmpiDrawing::Size savedSize(m->getLayoutRect().getWidth(), m->getLayoutRect().getHeight());
				GmpiDrawing::Size desired;
				GmpiDrawing::Size actualSize;
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
					GmpiDrawing::Size defaultSize(defaultDimensions, defaultDimensions);
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
				GmpiDrawing::Size desiredMax(0, 0);
				m->measure(GmpiDrawing::Size(10000, 10000), &desiredMax);

				m->isResizableX = desired.width != desiredMax.width;
				m->isResizableY = desired.height != desiredMax.height;
*/
/*
				if (debug)
				{
					_RPT4(_CRT_WARN, "arrange r[ %f %f %f %f]\n", m->getBounds().left, m->getBounds().top, m->getBounds().left + actualSize.width, m->getBounds().top + actualSize.height);
				}
*/
				m->arrange(GmpiDrawing::Rect(m->getLayoutRect().left, m->getLayoutRect().top, m->getLayoutRect().left + actualSize.width, m->getLayoutRect().top + actualSize.height));

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
			{
				m->arrange(GmpiDrawing::Rect(0, 0, 10, 10));
			}
		}

		isArranged = true;
		return gmpi::MP_OK;
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

	void ViewBase::PreGraphicsRedraw()
	{
		// Get any meter updates from DSP. ( See also CSynthEditAppBase::OnTimer() )
		Presenter()->GetPatchManager()->serviceGuiQueue();

		processUnidirectionalModules();
	}

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

					int32_t handle{};
					mod->getHandle(handle);

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
		return (int32_t) onKey(c, (gmpi::drawing::Point*) &lastMovePoint);
	}

	gmpi::ReturnCode ViewBase::getDrawingFactory(gmpi::api::IUnknown** returnFactory)
	{
#ifdef _WIN32
		*returnFactory = static_cast<gmpi::drawing::api::IFactory*>(&frameWindow->DrawingFactory->gmpiFactory);
		return gmpi::ReturnCode::Ok;
#endif
        return gmpi::ReturnCode::NoSupport;
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
				EndCableDrag({ -10000, -10000 }, cable);
				return gmpi::ReturnCode::Handled;
			}
			else if (!draggingNewModuleId.empty())
			{
				DragNewModule(nullptr);
				return gmpi::ReturnCode::Handled;
			}
			break;

		case 'n':
		case 'N':
			if (pointerPosOrNull)
			{
				if (DoModulePicker(*pointerPosOrNull))
					return gmpi::ReturnCode::Handled;
			}
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

		GmpiDrawing::Size desiredMax{};
		modulePicker->measure(GmpiDrawing::Size(100, 80), &desiredMax);
		modulePicker->arrange(GmpiDrawing::Rect(currentPointerPos.x, currentPointerPos.y, currentPointerPos.x + desiredMax.width, currentPointerPos.y + desiredMax.height));

//		modulePicker->init();

		const auto invalidRect = modulePicker->GetClipRect();
		getGuiHost()->invalidateRect(&invalidRect);
		return true;
	}

	void ViewBase::DismissModulePicker()
	{
		const auto invalidRect = modulePicker->GetClipRect();
		RemoveChild(modulePicker);
		modulePicker = nullptr;
		getGuiHost()->invalidateRect(&invalidRect);
	}

	IViewChild* ViewBase::Find(GmpiDrawing::Point& p)
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
