
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
			const float thickWidth = 3.0f;

			// Background
			{
				// fill in the area arround the drawing area. avoiding overdraw.
				GmpiDrawing::Rect editingBounds{ 0.0f, 0.0f, 7968.0f, 7968.0f };
				GmpiDrawing::Rect huge{ -100000.0f, -100000.0f, 100000.0f, 100000.0f };

				auto backgroundBrush = g.CreateSolidColorBrush(GmpiDrawing::Color(0x555555u));
				auto temp = huge;
				temp.bottom = editingBounds.top;
				g.FillRectangle(temp, backgroundBrush);

				temp = huge;
				temp.top = editingBounds.bottom;
				g.FillRectangle(temp, backgroundBrush);

				temp = huge;
				temp.top = editingBounds.top - 1.0f;
				temp.bottom = editingBounds.bottom + 1.0f;
				temp.right = editingBounds.left;
				g.FillRectangle(temp, backgroundBrush);

				temp.left = editingBounds.right;
				temp.right = huge.right;
				g.FillRectangle(temp, backgroundBrush);

				// fill the drawing area
				backgroundBrush.SetColor(backGroundColor);
				g.FillRectangle(editingBounds, backgroundBrush);
			}

			auto zoomFactor = g.GetTransform()._11; // horizontal scale.
			// BACKGROUND GRID LINES.
			auto brush = g.CreateSolidColorBrush(backGroundColor + 0x040404u); // grid color.
			if (zoomFactor > 0.5f)
			{
				GmpiDrawing::Rect cliprectF = g.GetAxisAlignedClip();
				GmpiDrawing::Rect cliprect{
					  cliprectF.left
					, cliprectF.top
					, cliprectF.right
					, cliprectF.bottom
				};

				cliprect.left = (std::max)(cliprect.left, 0.0f);
				cliprect.top = (std::max)(cliprect.top, 0.0f);
				cliprect.right = (std::min)(cliprect.right, 7968.0f);
				cliprect.bottom = (std::min)(cliprect.bottom, 7968.0f);

				constexpr int viewDimensions = 7968;
				constexpr int gridSize = 12; // *about that, dpi_ / 96;
				constexpr int gridBoarder = 2; // 2 grids
				constexpr int largeGridRatio = 5; // small grids per big grid.
				constexpr int totalGrids = viewDimensions / gridSize - 2 * gridBoarder;

				// quantize start x/y to grid.
				int startX = cliprect.left / gridSize;
				startX = (std::max)(startX, gridBoarder);
				startX = startX * gridSize - 1;

				int startY = cliprect.top / gridSize;
				startY = (std::max)(startY, gridBoarder);
				startY = startY * gridSize - 1;

				int endX = (cliprect.right + gridSize - 1) / gridSize;
				endX = (std::min)(endX, totalGrids + gridBoarder);
				endX = endX * gridSize + 1;

				int endY = (cliprect.bottom + gridSize - 1) / gridSize;
				endY = (std::min)(endY, totalGrids + gridBoarder);
				endY = endY * gridSize + 1;

				constexpr int largeGridSize = gridSize * largeGridRatio;

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
					g.DrawLine(x + 0.5f, startY + 0.5f, x + 0.5f, endY - 0.5f, brush, penWidth);
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

					g.DrawLine(startX + 0.5f, y + 0.5f, endX - 0.5f, y + 0.5f, brush, penWidth);
				}

				// outline entire grid to clean up 4 corners.
//				brush.SetColor(Color::Red); // debug
				g.DrawRectangle(
					Rect{
						gridSize * 2 - 0.5f, gridSize * 2 - 0.5f, 7968.0f - gridSize * 2 - 0.5f, 7968.0f - gridSize * 2 - 0.5f
					}
					, brush
					, thickWidth
				);
			}
		}
#endif

		return ViewBase::OnRender(drawingContext);
	}
} // namespace

 
