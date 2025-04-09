
#include <random>
#include <sstream>
#include <iostream>
#include "ContainerViewStruct.h"
#include "UgDatabase.h"
#include "modules/shared/unicode_conversion.h"
#include "RawConversions.h"
#include "ModuleView.h"
#include "ConnectorView.h"
#include "SubViewPanel.h"
#include "tinyxml/tinyxml.h"
#include "IGuiHost2.h"
#include "ModuleViewStruct.h"
#include "ConnectorViewStruct.h"

using namespace std;
using namespace gmpi;
using namespace gmpi_gui;
using namespace GmpiDrawing;

namespace SE2
{
	void ContainerViewStruct::BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap)
	{
		mouseOverObject = {};

#if _DEBUG
		{
			Json::StyledWriter writer;
			auto factoryXml = writer.write(*context);
			auto s = factoryXml;
		}
#endif

		// Modules.
		Json::Value& modules_json = (*context)["modules"];

		for (auto& module_json : modules_json)
		{
			const auto typeName = module_json["type"].asString();

			std::unique_ptr<ModuleView> module;

			if (typeName == "Line")
			{
				assert(!isIteratingChildren);
				children.push_back(std::make_unique<ConnectorView2>(&module_json, this));
			}
			else
			{
				if (typeName == "SE Structure Group2")
				{
					module = std::make_unique<ModuleViewPanel>(&module_json, this, guiObjectMap);
				}
				else
				{
					module = std::make_unique<ModuleViewStruct>(&module_json, this, guiObjectMap);
				}
			}

			if (module)
			{
				const auto isBackground = !module_json["ignoremouse"].empty();

				if ((module->getSelected() || isBackground) && Presenter()->editEnabled())
				{
					assert(!isIteratingChildren);
					children.push_back(module->createAdorner(this));
				}

				guiObjectMap.insert({ module->getModuleHandle(), module.get() });
				assert(!isIteratingChildren);
				children.push_back(std::move(module));
			}
		}

		// get Z-Order same as SE.
		std::reverse(std::begin(children), std::end(children));
	}

	ConnectorViewBase* ContainerViewStruct::createCable(CableType type, int32_t handleFrom, int32_t fromPin)
	{
		if (type == CableType::PatchCable)
			return new SE2::PatchCableView(this, handleFrom, fromPin, -1, -1);
		else
			return new SE2::ConnectorView2(this, handleFrom, fromPin, -1, -1);
	}

	int32_t ContainerViewStruct::OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext)
	{
		Graphics g(drawingContext);

#if 0 //def _DEBUG
		// Diagnose dirty rects.
		static int red = 0; // not in release
		red += 10;
		auto color = Color::FromBytes(red & 0xff, 0x77, 0x77);
		g.Clear(color);
#else

//		if (viewType == CF_STRUCTURE_VIEW)
		{
			// THEME
			const unsigned int backGroundColor = 0xACACACu; // Background color
			//const unsigned int backGroundColor = 0xa0a0a0u; // Background color (darker)
			//const unsigned int backGroundColor = 0x707070u; // Background color (much darker)
			const float thickWidth = 3.0f;

			g.Clear(Color(backGroundColor));

			auto zoomFactor = g.GetTransform()._11; // horizontal scale.
			// BACKGROUND GRID LINES.
			if (zoomFactor > 0.5f)
			{
				GmpiDrawing::Rect cliprect = g.GetAxisAlignedClip();

				//				auto brush = g.CreateSolidColorBrush(0xB0B0B0u); // grid color.
				auto brush = g.CreateSolidColorBrush(backGroundColor + 0x040404u); // grid color.

				const int gridSize = 12; // *about that, dpi_ / 96;
				const int gridBoarder = 2; // 2 grids
				const int largeGridRatio = 5; // small grids per big grid.
				int startX = static_cast<int32_t>(cliprect.left) / gridSize;
				startX = (std::max)(startX, gridBoarder);
				startX = startX * gridSize - 1;

				int startY = static_cast<int32_t>(cliprect.top) / gridSize;
				startY = (std::max)(startY, gridBoarder);
				startY = startY * gridSize - 1;

				constexpr int largeGridSize = gridSize * largeGridRatio;
				const int lastgrid = gridSize * gridBoarder + largeGridSize * ((static_cast<int32_t>(drawingBounds.getWidth()) - 2 * gridSize * gridBoarder) / largeGridSize);
				//				const int lastYgrid = gridSize * gridBoarder + largeGridSize * (static_cast<int32_t>(drawingBounds.getHeight() / largeGridSize - 1));

				int endX = (std::min)(lastgrid, static_cast<int32_t>(cliprect.right));
				int endY = (std::min)(lastgrid, static_cast<int32_t>(cliprect.bottom));

				int thickLineCounter = ((startX + gridSize * (largeGridRatio - gridBoarder)) / gridSize) % largeGridRatio;
				for (int x = startX; x < endX; x += gridSize)
				{
					float penWidth;
					if (++thickLineCounter == largeGridRatio)
					{
						penWidth = thickWidth;
						thickLineCounter = 0;
					}
					else
					{
						penWidth = 1;
					}
					g.DrawLine(x + 0.5f, startY + 0.5f, x + 0.5f, endY + 0.5f, brush, penWidth);
				}

				thickLineCounter = ((startY + gridSize * (largeGridRatio - gridBoarder)) / gridSize) % largeGridRatio;
				for (int y = startY; y < endY; y += gridSize)
				{
					float penWidth;
					if (++thickLineCounter == largeGridRatio)
					{
						penWidth = thickWidth;
						thickLineCounter = 0;
					}
					else
					{
						penWidth = 1;
					}

					g.DrawLine(startX + 0.5f, y + 0.5f, endX + 0.5f, y + 0.5f, brush, penWidth);
				}
			}
		}
		//else
		//{
		//	g.Clear(Color::LightGray);
		//}
#endif

		return ViewBase::OnRender(drawingContext);
	}
} // namespace

 
