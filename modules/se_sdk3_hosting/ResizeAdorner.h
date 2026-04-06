#pragma once

#include "./IViewChild.h"
#include "Drawing.h"
#include "ModuleView.h"

namespace SE2
{
	class ModuleView;

	class ResizeAdorner : public IViewChild
	{
	protected:
		static constexpr float SelectionFrameOffset = 1.5f;
		static constexpr float ResizeHandleRadius = 4;
		static constexpr float DragAreaheight = 4;

		ViewBase* parent{};
		ModuleView* module{};
		int32_t moduleHandle{};
		gmpi::drawing::Point pointPrev{};
		int currentNodeX = -1;
		int currentNodeY = -1;
		gmpi::drawing::Color color{};
		bool isResizableX{};
		bool isResizableY{};
		gmpi::drawing::Rect bounds{};
		gmpi::drawing::Rect prevClipArea;
		bool mouseHover{};

		struct node
		{
			int xIndex;
			int yIndex;
			gmpi::drawing::Point location;
		};

		std::vector<node> getNodes() const;
		void hitTestNodes(gmpi::drawing::Point point, int& hitNodeX, int& hitNodeY);

		std::tuple<float, int, int> hitTestWhat(gmpi::drawing::Point point);

	public:
		bool hasGripper = true; // and top handles. i.e. is Panel view.
		bool drawOutline = true;

		ResizeAdorner(ViewBase* pParent, ModuleView* pModule);
		~ResizeAdorner() override;

		void measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize) override;
		void arrange(gmpi::drawing::Rect finalRect) override;

		gmpi::drawing::Rect clientBoundsToAdorner(gmpi::drawing::Rect r);
		 // this is NOT outline of entire module, only lower half containing plugin gfx
		virtual gmpi::drawing::Rect getNodeRect() const;

		gmpi::drawing::Rect getLayoutRect() override;
		gmpi::drawing::Rect getClipArea() override;

		void OnMoved(gmpi::drawing::Rect& r) override;
		void OnNodesMoved(std::vector<gmpi::drawing::Point>& newNodes) override;

		// rendered only on panel
		void render(gmpi::drawing::Graphics& g) override;

		bool hitTestR(int32_t flags, gmpi::drawing::Rect selectionRect) override;
		float hitTestFuzzy(int32_t flags, gmpi::drawing::Point point) override;

		std::string getToolTip(gmpi::drawing::Point point) override;
		void receiveMessageFromAudio(void*) override;

		gmpi::ReturnCode setHover(bool isMouseOverMe) override;
		gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override;

		gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override;
		gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override;
		gmpi::ReturnCode onContextMenu(int32_t idx) override;
		gmpi::ReturnCode onKeyPress(wchar_t c) override;

		int32_t getModuleHandle() override;
		bool getSelected() override;
		void setSelected(bool selected) override;
		void preDelete() override;
		gmpi::drawing::Point getConnectionPoint(CableType cableType, int pinIndex) override;
	};

	class ResizeAdornerStructure : public ResizeAdorner
	{
	public:
		ResizeAdornerStructure(ViewBase* pParent, ModuleView* pModule);

		gmpi::drawing::Rect getNodeRect() const override;
		gmpi::drawing::Rect getClipArea() override;
	};
}
