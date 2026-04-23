
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
#include "helpers/PixelSnapper.h"
#include "gmpi_drawing_conversions.h"

using namespace std;
using namespace gmpi;
using namespace gmpi_gui;
using namespace gmpi::drawing;
using namespace legacy_converters;

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

		auto& moduleDb = *CModuleFactory::Instance();

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
				auto moduleInfo = moduleDb.GetById(JmUnicodeConversions::Utf8ToWstring(typeName));

				if (!moduleInfo->hasVisiblePins() ) //typeName == "SE Structure Group2")
					module = std::make_unique<ModuleViewPanel>(&module_json, this, guiObjectMap);
				else
					module = std::make_unique<ModuleViewStruct>(&module_json, this, guiObjectMap);
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

	gmpi::ReturnCode ContainerViewStruct::render(gmpi::drawing::api::IDeviceContext* drawingContext)
	{
		Graphics g(drawingContext);

		const Matrix3x2 originalTransform = g.getTransform();

		// pan and zoom
		const auto transformed = originalTransform * viewTransform;
		g.setTransform(transformed);

//		if (viewType == CF_STRUCTURE_VIEW)
		{
			// THEME
			const unsigned int backGroundColor = 0xACACACu; // Background color

			// Background
			{
				// fill in the area arround the drawing area. avoiding overdraw.
				gmpi::drawing::Rect editingBounds{ 0.0f, 0.0f, (float)viewDimensions, (float)viewDimensions };
				gmpi::drawing::Rect huge{ -100000.0f, -100000.0f, 100000.0f, 100000.0f };

				auto backgroundBrush = g.createSolidColorBrush(gmpi::drawing::colorFromHex(0x555555u));
				auto temp = huge;
				temp.bottom = editingBounds.top;
				g.fillRectangle(temp, backgroundBrush);

				temp = huge;
				temp.top = editingBounds.bottom;
				g.fillRectangle(temp, backgroundBrush);

				temp = huge;
				temp.top = editingBounds.top - 1.0f;
				temp.bottom = editingBounds.bottom + 1.0f;
				temp.right = editingBounds.left;
				g.fillRectangle(temp, backgroundBrush);

				temp.left = editingBounds.right;
				temp.right = huge.right;
				g.fillRectangle(temp, backgroundBrush);

				// fill the drawing area
				backgroundBrush.setColor(backGroundColor);
				g.fillRectangle(editingBounds, backgroundBrush);
			}

			auto zoom = g.getTransform()._11; // horizontal scale.
			// BACKGROUND GRID LINES.
			auto brush = g.createSolidColorBrush(backGroundColor + 0x040404u); // grid color.
			if (zoom > 0.1f)
			{
				pixelSnapper2 snap( g.getTransform(), drawingHost->getRasterizationScale() );

				const auto thinLine = snap.thickness(1.0f);
				const auto thickLine = snap.thickness(3.0f);

				const auto drawFinegrid = zoom > 0.6f;

				gmpi::drawing::Rect cliprectF = g.getAxisAlignedClip();
				gmpi::drawing::Rect cliprect{
					  cliprectF.left
					, cliprectF.top
					, cliprectF.right
					, cliprectF.bottom
				};

				cliprect.left = (std::max)(cliprect.left, 0.0f);
				cliprect.top = (std::max)(cliprect.top, 0.0f);
				cliprect.right = (std::min)(cliprect.right, (float)viewDimensions);
				cliprect.bottom = (std::min)(cliprect.bottom, (float)viewDimensions);


				constexpr int gridSize = 12; // *about that, dpi_ / 96;
				constexpr int gridBoarder = 2; // 2 grids
				constexpr int largeGridRatio = 5; // small grids per big grid.
				constexpr int totalGrids = viewDimensions / gridSize - 2 * gridBoarder;

				// quantize start x/y to grid.
				int startX = static_cast<int>(cliprect.left) / gridSize;
				startX = (std::max)(startX, gridBoarder);
				startX = startX * gridSize;

				int startY = static_cast<int>(cliprect.top) / gridSize;
				startY = (std::max)(startY, gridBoarder);
				startY = startY * gridSize;

				int endX = (static_cast<int>(cliprect.right) + gridSize) / gridSize;
				endX = (std::min)(endX, totalGrids + gridBoarder);
				endX = endX * gridSize + 1;

				int endY = (static_cast<int>(cliprect.bottom) + gridSize) / gridSize;
				endY = (std::min)(endY, totalGrids + gridBoarder);
				endY = endY * gridSize + 1;

				// vertical lines.
				int thickLineCounter = ((startX + gridSize * (largeGridRatio - gridBoarder)) / gridSize) % largeGridRatio;
				const float y1 = startY + 0.5f;
				const float y2 = endY   - 0.5f;
				for (int x = startX; x < endX; x += gridSize)
				{
					const auto xo = snap.snapX(static_cast<float>(x));

					if (++thickLineCounter == largeGridRatio)
					{
						thickLineCounter = 0;

						const auto& line = thickLine;
						const auto xsnapped = xo + line.center_offset;
						g.drawLine({ xsnapped, y1 }, { xsnapped, y2 }, brush, line.width);
					}
					else
					{
						if (!drawFinegrid)
							continue;

						const auto& line = thinLine;
						const auto xsnapped = xo + line.center_offset;
						g.drawLine({ xsnapped, y1 }, { xsnapped, y2 }, brush, line.width);

//						_RPTN(0, "draw fine vertical x=%f w=%f\n", transformPoint(snap.inverted, { xsnapped, 0.0f }).x, thinLine.width / drawingHost->getRasterizationScale());
					}
				}

				// horizonal lines.
				{
					thickLineCounter = ((startY + gridSize * (largeGridRatio - gridBoarder)) / gridSize) % largeGridRatio;
					const float x1 = startX + 0.5f;
					const float x2 = endX   - 0.5f;

					for(int y = startY; y < endY; y += gridSize)
					{
						auto yo = snap.snapY(static_cast<float>(y));

						if(++thickLineCounter == largeGridRatio)
						{
							thickLineCounter = 0;

							const auto& line = thickLine;
							const auto ysnapped = yo + line.center_offset;
							g.drawLine({ x1, ysnapped }, { x2, ysnapped }, brush, line.width);
						}
						else
						{
							if(!drawFinegrid)
								continue;

							const auto& line = thinLine;
							const auto ysnapped = yo + line.center_offset;
							g.drawLine({ x1, ysnapped }, { x2, ysnapped }, brush, line.width);
						}
					}
				}

				// outline entire grid to clean up 4 corners.
				g.drawRectangle(
					Rect{
						gridSize * 2 - 0.5f, gridSize * 2 - 0.5f, (float)viewDimensions - gridSize * 2 - 0.5f, (float)viewDimensions - gridSize * 2 - 0.5f
					}
					, brush
					, thickLine.width
				);
/* check alignment
auto p = 5 * 12.0f;
auto testBrush = g.createSolidColorBrush(Colors::Red);
g.drawLine(p, 100.f, p, -100.f, testBrush);
g.drawLine(100.f, p, -100.f, p, testBrush);
*/
			}
		}

		const auto r = ViewBase::render(drawingContext);

		g.setTransform(originalTransform);

		return r;
	}
} // namespace

 
