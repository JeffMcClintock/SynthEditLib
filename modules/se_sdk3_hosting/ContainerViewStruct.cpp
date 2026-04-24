
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

			renderGrid(g, colorFromHex(backGroundColor + 0x040404u));
		}

		const auto r = ViewBase::render(drawingContext);

		g.setTransform(originalTransform);

		return r;
	}
} // namespace

 
