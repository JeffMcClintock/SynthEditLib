
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
using namespace gmpi_gui;
using namespace GmpiDrawing;
using namespace legacy_converters;

namespace SE2
{
	void ContainerViewPanel::Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap)
	{
		skinName_ = (*context)["skin"].asString();

		ViewBase::Refresh(context, guiObjectMap);
	}

	int32_t ContainerViewPanel::OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext)
	{
		Graphics g(drawingContext);

		const Matrix3x2 originalTransform = g.GetTransform();

		// pan and zoom
		const auto viewTransformL = originalTransform * toLegacy(viewTransform);
		g.SetTransform(viewTransformL);

#if 0 //def _DEBUG
		// Diagnose dirty rects.
		static int red = 0; // not in release
		red += 10;
		auto color = Color::FromBytes(red & 0xff, 0x77, 0x77);
		g.Clear(color);
#else

		g.Clear(Color::LightGray);
#endif

		const auto r = ViewBase::OnRender(drawingContext);

		g.SetTransform(originalTransform);

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

 
