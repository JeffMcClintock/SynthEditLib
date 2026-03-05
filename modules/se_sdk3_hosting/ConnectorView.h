#pragma once
#include "IViewChild.h"
#include "modules/se_sdk2/se_datatypes.h"
#include "../shared/VectorMath.h"

namespace SE2
{
	class ConnectorViewBase : public ViewChild
	{
	protected:
		const int cableDiameter = 6;
		static const int lineWidth_ = 3;
		
		int32_t fromModuleH;
		int32_t toModuleH;
		int fromModulePin;
		int toModulePin;
		char datatype = DT_FSAMPLE;
		bool drawArrows = false;
		int highlightFlags = 0;
#if defined( _DEBUG )
		float cancellation = 0.0f;
#endif

		gmpi::drawing::PathGeometry geometry;
		gmpi::drawing::StrokeStyle strokeStyle;

	public:

		int draggingFromEnd = -1;
		gmpi::drawing::Point from_;
		gmpi::drawing::Point to_;

		CableType type = CableType::PatchCable;

		// Fixed connections.
		ConnectorViewBase(Json::Value* pDatacontext, ViewBase* pParent);

		// Dynamic patch-cables.
		ConnectorViewBase(ViewBase* pParent, int32_t pfromUgHandle, int fromPin, int32_t ptoUgHandle, int toPin) :
			ViewChild(pParent, -1)
			, fromModuleH(pfromUgHandle)
			, toModuleH(ptoUgHandle)
			, fromModulePin(fromPin)
			, toModulePin(toPin)
		{
		}

		void setHighlightFlags(int flags);

		virtual bool useDroop()
		{
			return false;
		}

		void pickup(int draggingFromEnd, gmpi::drawing::Point pMousePos);

		int32_t toModuleHandle()
		{
			return toModuleH;
		}

		int32_t fromModuleHandle()
		{
			return fromModuleH;
		}
		int32_t fromPin()
		{
			return fromModulePin;
		}
		int32_t toPin()
		{
			return toModulePin;
		}

		void measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize) override;
		void arrange(gmpi::drawing::Rect finalRect) override;

		void OnModuleMoved();
		virtual void CalcBounds() = 0;
		virtual void CreateGeometry() = 0;

		// IViewChild
		gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		void OnMoved(gmpi::drawing::Rect& newRect) override {}
		void OnNodesMoved(std::vector<gmpi::drawing::Point>& newNodes) override {}

		gmpi::ReturnCode onContextMenu(int32_t idx) override;

		int32_t fixedEndModule()
		{
			return draggingFromEnd == 1 ? fromModuleH : toModuleH;
		}
		int32_t fixedEndPin()
		{
			return draggingFromEnd == 1 ? fromModulePin : toModulePin;
		}
		gmpi::drawing::Point dragPoint()
		{
			return draggingFromEnd == 1 ? to_ : from_;
		}
	};

	struct sharedGraphicResources_patchcables
	{
		static const int numColors = 5;
		gmpi::drawing::SolidColorBrush brushes[numColors][2]; // 5 colors, 2 opacities.
		gmpi::drawing::SolidColorBrush highlightBrush;
		gmpi::drawing::SolidColorBrush outlineBrush;

		sharedGraphicResources_patchcables(gmpi::drawing::Graphics& g)
		{
			using namespace gmpi::drawing;
			highlightBrush = g.createSolidColorBrush(Colors::White);
			outlineBrush = g.createSolidColorBrush(Colors::Black);

			uint32_t datatypeColors[numColors] = {
				0x00B56E, // 0x00A800, // green
				0xED364A, // 0xbc0000, // red
				0xFFB437, // 0xD9D900, // yellow
				0x3695EF, // 0x0000bc, // blue
//				0x000000, // black 0x00A8A8, // green-blue
				0x8B4ADE, // 0xbc00bc, // purple
			};

			for(int i = 0; i < numColors; ++i)
			{
				brushes[i][0] = g.createSolidColorBrush(datatypeColors[i]);					// Visible
				brushes[i][1] = g.createSolidColorBrush(colorFromHex(datatypeColors[i], 0.5f));	// Faded
			}
		}
	};

	// Dynamic patch-cables.
	// Fancy end-user-connected Patch-Cables.
	class PatchCableView : public ConnectorViewBase
	{
		bool isHovered = false;
		bool isShownCached = true;
		std::shared_ptr<sharedGraphicResources_patchcables> drawingResources;
		int colorIndex = 0;

		inline constexpr static float mouseNearEndDist = 20.0f; // should be more than mouseNearWidth, else looks glitchy.

	public:

		PatchCableView(ViewBase* pParent, int32_t pfromUgHandle, int fromPin, int32_t ptoUgHandle, int toPin, int pColor = -1) :
			ConnectorViewBase(pParent, pfromUgHandle, fromPin, ptoUgHandle, toPin)
		{
			if (pColor >= 0)
			{
				colorIndex = pColor;
			}
			else
			{
				// index -1 use next color cyclically
				// index -2 use current color

				static int nextColorIndex = 0;
				if (pColor == -1 && ++nextColorIndex >= sharedGraphicResources_patchcables::numColors)
					nextColorIndex = 0;

				colorIndex = nextColorIndex;
			}

			datatype = DT_FSAMPLE;
			drawArrows = false;
		}

		~PatchCableView();

		bool useDroop() override
		{
			return true;
		}

		int getColorIndex()
		{
			return colorIndex;
		}

		virtual void CreateGeometry() override;
		virtual void CalcBounds() override;

		bool isShown() override; // Indicates if module should be drawn or not (because of 'Show on Parent' state).
		void OnVisibilityUpdate();
		void render(gmpi::drawing::Graphics& g) override;
		gmpi::ReturnCode setHover(bool isMouseOverMe) override;

		gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override;
		sharedGraphicResources_patchcables* getDrawingResources(gmpi::drawing::Graphics& g);
	};
}
