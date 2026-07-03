#pragma once
#include "ConnectorView.h"
#include "../shared/VectorMath.h"

namespace SE2
{
	struct sharedGraphicResources_connectors
	{
		gmpi::drawing::SolidColorBrush brushes[14]; // one per EPlugDataType (DT_ENUM..DT_OBJECT)
		gmpi::drawing::SolidColorBrush errorBrush;
		gmpi::drawing::SolidColorBrush emphasiseBrush;
		gmpi::drawing::SolidColorBrush selectedBrush;
		gmpi::drawing::SolidColorBrush draggingBrush;

		sharedGraphicResources_connectors(gmpi::drawing::Graphics& g)
		{
			using namespace gmpi::drawing;
			brushes[0] = g.createSolidColorBrush(Colors::Black);

			errorBrush = g.createSolidColorBrush(Colors::Red);
			emphasiseBrush = g.createSolidColorBrush(Colors::Lime);
			selectedBrush = g.createSolidColorBrush(Colors::DodgerBlue);
			draggingBrush = g.createSolidColorBrush(Colors::Orange);

			uint32_t datatypeColors[] = {
				0x00A800, // [ 0] DT_ENUM        green
				0xbc0000, // [ 1] DT_TEXT        red
				0xD9D900, // [ 2] DT_MIDI2       yellow
				0x00bcbc, // [ 3] DT_DOUBLE      cyan
				0x000000, // [ 4] DT_BOOL        black
				0x0000bc, // [ 5] DT_FSAMPLE     blue
				0x00A8A8, // [ 6] DT_FLOAT       green-blue
				0x008989, // [ 7] DT_VST_PARAM   unused
				0xbc5c00, // [ 8] DT_INT         orange
				0xb45e00, // [ 9] DT_INT64       orange
				0xbc00bc, // [10] DT_BLOB        purple
				0xbc00bc, // [11] DT_STRUCT      purple (struct)
				0xbc0000, // [12] DT_STRING_UTF8 red
				0xbc00bc, // [13] DT_OBJECT      purple
			};

			assert(std::size(datatypeColors) <= std::size(brushes));

			for(size_t i = 0; i < std::size(brushes); ++i)
				brushes[i] = g.createSolidColorBrush(datatypeColors[i]);
		}
	};

	// Rounded U-turn that replaces the sharp 'elbow' where a line doubles back behind its
	// module. Computed by calcEndArc() in ConnectorViewStruct.cpp.
	struct EndArcInfo
	{
		bool valid = false;
		gmpi::drawing::Point S{}; // arc start: end of the short horizontal stub at the pin
		gmpi::drawing::Point T{}; // arc end: where the straight run leaves the circle
		bool clockwise = false;   // sweep direction when traversed pin -> S -> T
		bool largeArc = false;    // turn of more than 180 degrees (hairpin)
	};

	// Plain connections on structure view only.
	class ConnectorView2 : public ConnectorViewBase
	{
		static const int highlightWidth = 4;
		static const int NodeRadius = 4;
		const float arrowLength = 8;
		const float arrowWidth = 4;
		const float hitTestWidth = 5;

		enum ELineType { CURVEY, ANGLED };
		ELineType lineType_ = ANGLED;

		std::vector<gmpi::drawing::Point> nodes;
		std::vector<gmpi::drawing::PathGeometry> segmentGeometrys;
		EndArcInfo fromArc, toArc; // U-turn arcs of a doubled-back line, set by CreateGeometry()

		int draggingNode = -1;
		int hoverNode = -1;
		int hoverSegment = -1;
		gmpi::drawing::Point arrowPoint;
		Gmpi::VectorMath::Vector2D arrowDirection;

		std::shared_ptr<sharedGraphicResources_connectors> drawingResources;
		sharedGraphicResources_connectors* getDrawingResources(gmpi::drawing::Graphics& g);
		bool mouseHover = {};
		static gmpi::drawing::Point pointPrev; // for dragging nodes

		// returns { distance, hoverNode, hoverSegment }. distance: 0 = solid hit, 0..fuzzyHitTestLimit = fuzzy, totalMiss otherwise.
		std::tuple<float, int, int> hitTestWhat(gmpi::drawing::Point point);

		// The points a doubled-back CURVEY spline passes through: the U-turn arc tangent
		// points (or the pin + sharp elbow where an arc didn't fit) bracketing the user nodes.
		std::vector<gmpi::drawing::Point> curveyUTurnSeed(float endAdjust) const;

	public:
		// Dynamic patch-cables.
		ConnectorView2(Json::Value* pDatacontext, ViewBase* pParent) :
			ConnectorViewBase(pDatacontext, pParent)
		{
			auto& object_json = *datacontext;

			type = CableType::StructureCable;

			lineType_ = (ELineType) object_json.get("lineType", (int)ANGLED).asInt();

			auto& nodes_json = object_json["nodes"];

			if (!nodes_json.empty())
			{
				for (auto& node_json : nodes_json)
				{
					nodes.push_back(gmpi::drawing::Point(node_json["x"].asFloat(), node_json["y"].asFloat()));
				}
			}
		}

		ConnectorView2(ViewBase* pParent, int32_t pfromUgHandle, int fromPin, int32_t ptoUgHandle, int toPin) :
			ConnectorViewBase(pParent, pfromUgHandle, fromPin, ptoUgHandle, toPin)
		{
			type = CableType::StructureCable;
		}

		void CalcArrowGeometery(gmpi::drawing::GeometrySink & sink, gmpi::drawing::Point ArrowTip, Gmpi::VectorMath::Vector2D v1);

		void CreateGeometry() override;
		std::vector<gmpi::drawing::PathGeometry>& GetSegmentGeometrys();
		void CalcBounds() override;
		void render(gmpi::drawing::Graphics& g) override;
		gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override;
		float hitTestFuzzy(int32_t flags, gmpi::drawing::Point point) override;
		bool hitTestR(int32_t flags, gmpi::drawing::Rect selectionRect) override;
		void measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize) override;
		gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override
		{
			return gmpi::ReturnCode::Unhandled;
		}
#if 0 // todo
		int32_t populateContextMenu(float /*x*/, float /*y*/, gmpi::api::IUnknown* /*contextMenuItemsSink*/) override
		{
			return gmpi::MP_UNHANDLED;
		}
#endif
		gmpi::ReturnCode setHover(bool mouseIsOverMe) override;

		void OnMoved(gmpi::drawing::Rect& newRect) override
		{
		}
		void OnNodesMoved(std::vector<gmpi::drawing::Point>& newNodes) override;
	};
}
