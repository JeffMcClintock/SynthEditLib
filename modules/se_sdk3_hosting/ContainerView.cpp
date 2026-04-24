
#include <random>
#include <sstream>
#include <iostream>
#include "ContainerView.h"
#include "UgDatabase.h"
#include "modules/shared/unicode_conversion.h"
#include "RawConversions.h"
#include "ModuleView.h"
#include "ConnectorView.h"
#include "SubViewPanel.h"
#include "tinyxml/tinyxml.h"
#include "IGuiHost2.h"
#include "gmpi_drawing_conversions.h"

using namespace std;
using namespace gmpi;
//using namespace gmpi_gui;
using namespace gmpi::drawing;
using namespace legacy_converters;

namespace SE2
{
	void ContainerViewPanel::Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap)
	{
		skinName_ = (*context)["skin"].asString();

		ViewBase::Refresh(context, guiObjectMap);
	}

	gmpi::ReturnCode ContainerViewPanel::render(gmpi::drawing::api::IDeviceContext* drawingContext)
	{
		Graphics g(drawingContext);

		const Matrix3x2 originalTransform = g.getTransform();

		// pan and zoom
		const auto viewTransformL = originalTransform * viewTransform;
		g.setTransform(viewTransformL);

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
			backgroundBrush.setColor(Colors::LightGray);
			g.fillRectangle(editingBounds, backgroundBrush);
		}

		renderGrid(g, colorFromHex(0xDDDDDDu)); // slightly lighter than LightGray background

		const auto r = ViewBase::render(drawingContext);

#ifdef _DEBUG
		{
			constexpr float cx = viewDimensions * 0.5f;
			constexpr float arm = 20.0f;
			auto crossBrush = g.createSolidColorBrush(Colors::Orange);
			g.drawLine({ cx - arm, cx }, { cx + arm, cx }, crossBrush, 1.0f);
			g.drawLine({ cx, cx - arm }, { cx, cx + arm }, crossBrush, 1.0f);
		}
#endif

		g.setTransform(originalTransform);

		return r;
	}

	void ContainerViewPanel::BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap)
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

			assert(typeName != "Line");  // no lines on GUI.
			assert(typeName != "SE Structure Group2");
#ifdef _DEBUG
			// avoid trying to create unavailable modules
			const auto typeId = JmUnicodeConversions::Utf8ToWstring(typeName);
			auto moduleInfo = CModuleFactory::Instance()->GetById(typeId);
//			if (moduleInfo)
#endif
			auto module = std::make_unique<ModuleViewPanel>(&module_json, this, guiObjectMap);

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

	ConnectorViewBase* ContainerViewPanel::createCable(CableType type, int32_t handleFrom, int32_t fromPin)
	{
		return new SE2::PatchCableView(this, handleFrom, fromPin, -1, -1);
	}

} // namespace

 
