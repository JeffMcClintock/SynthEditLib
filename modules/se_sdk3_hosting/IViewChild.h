#pragma once

// DELETE THIS FILE #include "IModelBase.h"
//#include "xplatform.h"
#include "GmpiUiDrawing.h"
#include "module_register.h"
#include "jsoncpp/json/json.h"

namespace SE2
{
	class IPresenter;
	enum class CableType { PatchCable, StructureCable };

	// Children of a view inherit from this class. They may forward these calls to an SDK module.
	// This class needs to handle to translation of coordinates for mouse and drawing to/from the SDK module.
	class IViewChild
	{
	public:
		static constexpr float fuzzyHitTestLimit = 12.f;

		virtual ~IViewChild() {}

		// Similar to IDrawingClient for convenience. But don't confuse that with compatible or interchangeable.

		// layout
		virtual void measure(const gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize) = 0;
		virtual void arrange(const gmpi::drawing::Rect finalRect) = 0;
		virtual gmpi::drawing::Rect getClipArea() = 0;
		virtual gmpi::drawing::Rect getLayoutRect() = 0;

		// drawing
		virtual void render(gmpi::drawing::Graphics& g) = 0;

		// advanced hit testing
//		virtual gmpi::ReturnCode hitTest(int32_t flags, gmpi::drawing::Point point) = 0;
		virtual bool hitTestR(int32_t flags, gmpi::drawing::Rect rect) = 0;
		virtual float hitTestFuzzy(int32_t flags, gmpi::drawing::Point point) = 0;

		// Mouse events.
		virtual gmpi::ReturnCode setHover(bool isMouseOverMe) = 0;
		virtual gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) = 0;

		virtual gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) = 0;
		virtual gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) = 0;
		virtual gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) = 0;
		virtual gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) = 0;

		// right-click menu
		virtual gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) = 0;
		virtual gmpi::ReturnCode onContextMenu(int32_t idx) = 0;

		// keyboard events.
		virtual gmpi::ReturnCode onKeyPress(wchar_t c) = 0;

		virtual void receiveMessageFromAudio(void*) = 0;
		virtual void preDelete() = 0; // for optimisation when removing a single module, so that we can avoid destroying and recreating entire view.

		// Additions
		virtual std::string getToolTip(gmpi::drawing::Point point) = 0;
		virtual int32_t getModuleHandle() = 0;
		virtual bool getSelected() = 0;
		virtual void setSelected(bool selected) = 0;
		virtual void OnMoved(gmpi::drawing::Rect& newRect) = 0;
		virtual void OnNodesMoved(std::vector<gmpi::drawing::Point>& newNodes) = 0;
		virtual void OnClickedButDidntDrag() {}
		virtual bool isVisable() // indicates if module has a visible graphical component.
		{
			return true;
		}
		virtual bool isShown() // Indicates if module should be drawn or not (because of 'Show on Parent' state).
		{
			return true;
		}
		virtual bool isDraggable(bool editEnabled)
		{
			// default is that anything can be dragged in the editor.
			return editEnabled;
		}
		virtual void OnCableDrag(class ConnectorViewBase* dragline) // i.e. during cable being dragged around.
		{
		}
		virtual bool EndCableDrag(gmpi::drawing::Point point, class ConnectorViewBase* dragline, int32_t keyFlags)
		{
			return false;
		}
		virtual void OnCableDrag(ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistance, class ModuleView*& bestModule, int& bestPinIndex)
		{
		}

		virtual gmpi::drawing::Point getConnectionPoint(CableType cableType, int pinIndex) = 0;

		virtual void setDirty() {}
		virtual bool getDirty()
		{
			return false;
		}
		virtual void process() {} // clears dirty flag.
	};


	class ViewChild : public IViewChild
	{
		bool selected = {};
	public:
		gmpi::drawing::Rect bounds_; // should be RectL since only integer co-ords are valid.
		Json::Value* datacontext = {};
		int handle = -1;
		class ViewBase* parent = {};

		ViewChild(Json::Value* pContext, ViewBase* pParent);

		ViewChild(ViewBase* pParent, int pHandle) :
			handle(pHandle)
			, datacontext(nullptr)
			, parent(pParent)
		{
		}

		~ViewChild() override;

		// IViewChild
		void measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize) override
		{
			*returnDesiredSize = availableSize;
		}
		void arrange(gmpi::drawing::Rect finalRect) override
		{
			bounds_ = finalRect;
		}

		// bounds for the purpose of layout. May not include all drawn pixels.
		gmpi::drawing::Rect getLayoutRect() override
		{
			return bounds_;
		}

		// bounds for the purpose of invalidating every single drawn pixel, Including those outside layout boundary.
		gmpi::drawing::Rect getClipArea() override
		{
			return bounds_;
		}

		int32_t getModuleHandle() override
		{
			return handle;
		}
		// IViewChild //////////////////////////////////////

		bool editEnabled();
		bool getSelected() override
		{
			return selected;
		}

		// Can't use pure passive view for selection because module may have captured mouse,
		// so we can't refresh by destroying and recreating entire view.
		void setSelected(bool pselected) override
		{
			selected = pselected;
		}

		//gmpi::ReturnCode hitTest(int32_t flags, gmpi::drawing::Point point) override
		//{
		//	return pointInRect(point, getLayoutRect()) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
		//}
		bool hitTestR(int32_t flags, gmpi::drawing::Rect selectionRect) override
		{
			return overlaps(selectionRect, getLayoutRect());
		}
		float hitTestFuzzy(int32_t flags, gmpi::drawing::Point point) override
		{
			return hitTest(point, flags) == gmpi::ReturnCode::Ok ? 0.0f : 1000.0f;
		}

		gmpi::ReturnCode setHover(bool isMouseOverMe) override
		{
			(void)isMouseOverMe;
			return gmpi::ReturnCode::Ok;
		}

		gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
		{
			return pointInRect(point, getLayoutRect()) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onContextMenu(int32_t idx) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onKeyPress(wchar_t c) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		std::string getToolTip(gmpi::drawing::Point point) override
		{
			return {};
		}
		void receiveMessageFromAudio(void*) override
		{
		}

		IPresenter* Presenter();

		gmpi::drawing::Factory getFactory();

		gmpi::drawing::Point getConnectionPoint(CableType cableType, int pinIndex) override
		{
			return {};
		}

		bool imCaptured();
		void preDelete() override {}
	};
}
