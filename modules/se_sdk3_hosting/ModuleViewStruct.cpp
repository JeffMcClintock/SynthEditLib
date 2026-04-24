#include <vector>
#include <sstream>
#include <iomanip>

#include "ModuleViewStruct.h"
#include "ContainerView.h"
#include "ConnectorView.h"
#include "UgDatabase.h"
#include "modules/shared/xplatform.h"
#include "InterfaceObject.h"
#include "cpu_accumulator.h"
#include "ResizeAdorner.h"
#include "helpers/SimplifyGraph.h"
#include "modules/se_sdk3_hosting/PresenterCommands.h"
#include "mfc_emulation.h"
#include "helpers/PixelSnapper.h"

using namespace gmpi;
using namespace std;
using namespace gmpi::drawing;

inline PathGeometry DataToGraph(Graphics& g, const std::vector<Point>& inData)
{
	auto geometry = g.getFactory().createPathGeometry();
	auto sink = geometry.open();
	bool first{ true };
	for (const auto& p : inData)
	{
		if (first)
		{
			sink.beginFigure(p);
			first = false;
		}
		else
		{
			sink.addLine(p);
		}
	}

	sink.endFigure(FigureEnd::Open);
	sink.close();

	return geometry;
}

namespace SE2
{
	std::chrono::time_point<std::chrono::steady_clock> ModuleViewStruct::lastClickedTime;
	GraphicsResourceCache<sharedGraphicResources_struct> ModuleViewStruct::drawingResourcesCache;

	ModuleViewStruct::ModuleViewStruct(Json::Value* context, class ViewBase* pParent, std::map<int, class ModuleView*>& guiObjectMap) : ModuleView(context, pParent)
	{
		auto& module_element = *context;
		const bool isContainer = moduleInfo->UniqueId() == L"Container";

		name = module_element.get("title", Json::Value("")).asString();
		muted = module_element.get("muted", Json::Value(false)).asBool();

/* we no longer lock structure
		const bool locked = false;
		if(isContainer)
			locked = module_element.get("locked", Json::Value(false)).asBool();
*/

		// create list of pins.
		for (auto& it : moduleInfo->gui_plugs)
		{
			auto& pin = it.second;
			pinViewInfo info{};
			info.name = WStringToUtf8(pin->GetName());
			info.direction = pin->GetDirection();
			info.datatype = pin->GetDatatype();
			info.isGuiPlug = true;
			info.plugDescID = pin->getPlugDescID();
			info.isIoPlug = /*info.isSpareIoPlug =*/ pin->isContainerIoPlug(); // was isIoPlug()
			info.isAutoduplicatePlug = pin->autoDuplicate();// && pin->GetNumConnections() == 0;
			info.isVisible = 0 == (pin->GetFlags() & (IO_PARAMETER_SCREEN_ONLY | IO_MINIMISED | IO_HIDE_PIN));
			plugs_.push_back(info);
		}

		for (auto& it : moduleInfo->plugs)
		{
			auto& pin = it.second;
			pinViewInfo info{};
			info.name = WStringToUtf8(pin->GetName());
			info.direction = pin->GetDirection();
			info.datatype = pin->GetDatatype();
			info.isGuiPlug = false;
			info.plugDescID = pin->getPlugDescID();
			info.isIoPlug = /*info.isSpareIoPlug =*/ pin->isContainerIoPlug(); // was isIoPlug()
			info.isAutoduplicatePlug = pin->autoDuplicate();
			info.isVisible = 0 == (pin->GetFlags() & (IO_PARAMETER_SCREEN_ONLY | IO_MINIMISED | IO_HIDE_PIN));
//			info.isVisible &= !locked || !info.isAutoduplicatePlug;
			plugs_.push_back(info);
		}

		{
			// Autoduplicating pins.
			const auto& pins_element = module_element["Pins"];
			if (!pins_element.isNull())
			{
				int pinId = 0;
				for (auto& pin_element : pins_element)
				{
					const auto& id_e = pin_element["Id"];
					if (!id_e.isNull())
						pinId = id_e.asInt();

					// IO Pins.
					auto& directionE = pin_element["Direction"];
					auto& datatypeE = pin_element["Datatype"];
					if (!directionE.empty() && !datatypeE.empty()) // I/O Plugs.
					{
						pinViewInfo info{};
						info.name = pin_element["name"].asString();
						info.direction = (char)directionE.asInt();
						info.datatype = (char)datatypeE.asInt();
						auto& isGuiPin = pin_element["GuiPin"];
						info.isGuiPlug = !isGuiPin.empty();
						info.plugDescID = pinId; // pin->getPlugDescID(nullptr);
						info.isIoPlug = true; // TODO!! pin->isIoPlug(nullptr);
						info.isVisible = true;
						info.isAutoduplicatePlug = false;
						info.isTiedToUnconnected = !pin_element["partial"].empty();

						plugs_.push_back(info);
					}
					else	// Other auto-duplicating plugs.
					{
						const auto& ac_e = pin_element["AutoCopy"];
						if (!ac_e.isNull())
						{
							int autoCopyId = ac_e.asInt();
							for (const auto& p : plugs_)
							{
								if (p.plugDescID == autoCopyId && p.isAutoduplicatePlug) // could possibly clash if both GUI and DSP autoduplicating plugins exist.
								{
									pinViewInfo info(p);
								if (!pin_element["name"].isNull())
									info.name = pin_element["name"].asString();
									// info.plugDescID = pinId; //? what
									info.isAutoduplicatePlug = false;

									plugs_.push_back(info);
									break;
								}
							}
						}
					else
					{
						// nameable (but not autoduplicate)
						const auto& name_e = pin_element["name"];
						if (!name_e.isNull())
							plugs_[pinId].name = name_e.asString();
					}
				}

					++pinId; // Allows pinIdx to default to 1 + prev Idx. TODO, only used by slider2, could add this to exportXml.
				}
			}
		}

		// Move spare last.
		for (auto it = plugs_.begin(); it != plugs_.end(); ++it)
		{
			if ((*it).isAutoduplicatePlug)
			{
				auto p = *it;
				plugs_.erase(it);
				plugs_.push_back(p);
				break;
			}
		}

		// Slider replacement needs pins re-ordered on GUI to match old-skool order.
// TODO: Upgrade all Sliders to new version on load, then ditch this.
		auto uniqueid = getModuleType()->UniqueId();
		if (uniqueid == L"SE Slider")
		{
			const int fromIdx = 12; // "Signal Out"
			const int toIdx = 6;
			auto temp = plugs_[fromIdx];
			auto it = plugs_.erase(plugs_.begin() + fromIdx);
			plugs_.insert(plugs_.begin() + toIdx, temp);
		}

		if (uniqueid == L"SE List Entry")
		{
			const int fromIdx = 8; // "Value Out"
			const int toIdx = 6;
			auto temp = plugs_[fromIdx];
			auto it = plugs_.erase(plugs_.begin() + fromIdx);
			plugs_.insert(plugs_.begin() + toIdx, temp);
		}

		// Index pins.
		int pinIndexCombined = 0; // ALL Pins combined
		for (auto& p : plugs_)
			p.indexCombined = pinIndexCombined++;

		// technically don't need DSP pins here.
		editorPinValues = std::make_unique< std::map<int, std::vector<uint8_t> >>();

		// Sort visually.
		/* screws up hit detectiosn
		const bool hasOldStyleListInterface = (moduleInfo->GetFlags() & CF_OLD_STYLE_LISTINTERFACE) != 0;

		std::sort(plugs_.begin(), plugs_.end(),
			[hasOldStyleListInterface](const pinViewInfo& a, const pinViewInfo& b) -> bool
		{
			// Pins are sorted by:
			// - normal vs IO Plug
			// - GUI (first), then DSP.
			// - Each group in ID order.
			// EXCEPT old-style Listinterface() (SDK2 and some internal modules) which are purely in ID order to retain old idx to pin mapping. for e.g. seGuiHostPlugGetVal
			if (a.isIoPlug != b.isIoPlug)
			{
				return a.isIoPlug < b.isIoPlug;
			}

			if (a.isAutoduplicatePlug != b.isAutoduplicatePlug)
			{
				return a.isAutoduplicatePlug < b.isAutoduplicatePlug;
			}

			if (a.isGuiPlug == b.isGuiPlug || hasOldStyleListInterface)
			{
				return a.plugDescID < b.plugDescID;
			}

			return a.isGuiPlug > b.isGuiPlug;
		});
		*/
		bool showControlsOnModule = false;
		if (isContainer)
		{
			// "Show Controls on Module" pin.
			const auto& pins_element = module_element["Pins"];
			if (!pins_element.isNull())
			{
				int pinId = 0;
				for (auto& pin_element : pins_element)
				{
					const auto& id_e = pin_element["Id"];
					if (!id_e.isNull())
						pinId = id_e.asInt();

					if (pinId == 1)
					{
						auto& default_element = pin_element["default"];
						if (!default_element.empty())
						{
							auto def = default_element.asString();
							showControlsOnModule = def == "1";
						}
						break;
					}

					++pinId;
				}

			}
		}

		if(showControlsOnModule)
			BuildContainer(context, guiObjectMap);
		else
			Build();
	}

	void ModuleViewStruct::measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize)
	{
		// Determine total plug height.
		auto visiblePlugsCount = std::count_if(plugs_.begin(), plugs_.end(),
			[](const pinViewInfo& p) -> bool
		{
			return p.isVisible;
		});

		constexpr auto plugDiameter = sharedGraphicResources_struct::plugDiameter;

		auto totalPlugHeight = visiblePlugsCount * plugDiameter;

        auto drawingFactory = getFactory();
		auto resources = getDrawingResources(drawingFactory);

		// calc text min width.
		float minTextWidth = 0.0f;
		for (const auto& pin : plugs_)
		{
			if (pin.isVisible)
			{
				auto s = resources->tf_plugs_left.getTextExtentU(pin.name);
				minTextWidth = (std::max)(minTextWidth, s.width);
			}
		}

		// Calc how much of width taken up by plugs etc.
		// Plugs section has plug circles sticking out on both sides.
		float minTextAndPlugsWidth = minTextWidth + 2.0f * plugTextHorizontalPadding + plugDiameter;

		// Calc width needed for embedded graphics, plus padding.
		// embedded gfx has half a plug sticking out each side.
		Size graphicsSectionDesiredSize(0, 0);
		if (pluginGraphics_GMPI || pluginGraphics)
		{
			float widthPadding = plugDiameter + clientPadding * 2.0f;
			float heightPadding = clientPadding * 2.0f;
			gmpi::drawing::Size remainingSize{ availableSize.width - widthPadding, availableSize.height - totalPlugHeight - heightPadding };

			gmpi::drawing::Size desiredSize{};
			if (pluginGraphics_GMPI)
			{
				pluginGraphics_GMPI->measure(&remainingSize, &desiredSize);
			}
			else
			{
				pluginGraphics->measure(*reinterpret_cast<GmpiDrawing_API::MP1_SIZE*>(&remainingSize), reinterpret_cast<GmpiDrawing_API::MP1_SIZE*>(&desiredSize));
			}

			graphicsSectionDesiredSize.width = desiredSize.width + widthPadding;
			graphicsSectionDesiredSize.height = desiredSize.height + heightPadding;
		}

		returnDesiredSize->height = (std::max)((float)plugDiameter, totalPlugHeight + graphicsSectionDesiredSize.height);

		// snap width to grid size.
		int width = static_cast<int32_t>(ceilf((std::max)(minTextAndPlugsWidth, graphicsSectionDesiredSize.width)));

		returnDesiredSize->width = (float) (((plugDiameter - 1 + width) / plugDiameter) * plugDiameter);

	}

	void ModuleViewStruct::arrange(gmpi::drawing::Rect finalRect)
	{
		if (getHeight(bounds_) != getHeight(finalRect) || getWidth(bounds_) != getWidth(finalRect))
			outlineGeometry = {};

		bounds_ = finalRect;

		auto visiblePlugsCount = std::count_if(plugs_.begin(), plugs_.end(),
			[](const pinViewInfo& p) -> bool
		{
			return p.isVisible;
		});

		constexpr auto plugDiameter = sharedGraphicResources_struct::plugDiameter;
		auto totalPlugHeight = visiblePlugsCount * plugDiameter;


		// measure plugin graphics against remaining height.
		if (pluginGraphics_GMPI || pluginGraphics)
		{
			float widthPadding = clientPadding * 2.0f;
			float heightPadding = static_cast<float>(totalPlugHeight) + clientPadding * 2.0f;

			Size remainingSize(getWidth(finalRect) - widthPadding, getHeight(finalRect) - heightPadding);

			Size desired;
			if (pluginGraphics_GMPI)
			{
				gmpi::drawing::Size remainingSizeU{ remainingSize.width, remainingSize.height };
				gmpi::drawing::Size desiredSizeU{};
				pluginGraphics_GMPI->measure(&remainingSizeU, &desiredSizeU);

				desired.width = static_cast<float>(desiredSizeU.width);
				desired.height = static_cast<float>(desiredSizeU.height);
			}
			else
			{
				pluginGraphics->measure(*reinterpret_cast<GmpiDrawing_API::MP1_SIZE*>(&remainingSize), reinterpret_cast<GmpiDrawing_API::MP1_SIZE*>(&desired));
			}

			float x = floorf((getWidth(finalRect) - desired.width) * 0.5f);
			float y = floorf(getHeight(finalRect) - desired.height - clientPadding);
			pluginGraphicsPos = gmpi::drawing::Rect(x, y, x + desired.width, y + desired.height);
			auto relativeRect = gmpi::drawing::Rect(0, 0, desired.width, desired.height);
			if (pluginGraphics_GMPI)
			{
				drawing::Rect gmpiRect{ 0, 0, relativeRect.right, relativeRect.bottom};
				pluginGraphics_GMPI->arrange(&gmpiRect);
			}
			else if (pluginGraphics)
			{
				pluginGraphics->arrange(*reinterpret_cast<GmpiDrawing_API::MP1_RECT*>(&relativeRect));
			}
		}

		// Calc clip rect.
		clipArea = bounds_;
		clipArea.bottom = (std::max)(clipArea.bottom, clipArea.top + plugDiameter); //Zero-height modules get expanded to 1 plug high.
		clipArea.left  -= 0.5f * sharedGraphicResources_struct::plugDiameter;
		clipArea.right += 0.5f * sharedGraphicResources_struct::plugDiameter;
		clipArea = inflateRect(clipArea, 2.0f); // cope with thick "selected" outline.
		clipArea.top -= 16; // expand upward to header text.

        auto drawingFactory = getFactory();
        auto resources = getDrawingResources(drawingFactory);

		// Expand left and right for long headers.
		auto headersize = resources->tf_header.getTextExtentU(name);
		float overhang = ceilf((headersize.width - getWidth(clipArea)) * 0.5f);
		if (overhang > 0.0f)
		{
			clipArea.left -= overhang;
			clipArea.right += overhang;
		}
	}

	gmpi::drawing::Rect ModuleViewStruct::getClipArea()
	{
		auto r = clipArea;

		if (pluginGraphics_GMPI)
		{
			drawing::Rect clientClipArea{};
			pluginGraphics_GMPI->getClipArea(&clientClipArea);
			clientClipArea = offsetRect(clientClipArea, { bounds_.left + pluginGraphicsPos.left, bounds_.top + pluginGraphicsPos.top });
			r = unionRect(r, clientClipArea);
		}
		else if (pluginGraphics4)
		{
			GmpiDrawing::Rect clientClipAreaLegacy{};
			pluginGraphics4->getClipArea(&clientClipAreaLegacy);
			gmpi::drawing::Rect clientClipArea{ clientClipAreaLegacy.left, clientClipAreaLegacy.top, clientClipAreaLegacy.right, clientClipAreaLegacy.bottom };
			clientClipArea = offsetRect(clientClipArea, { bounds_.left + pluginGraphicsPos.left, bounds_.top + pluginGraphicsPos.top });
			r = unionRect(r, clientClipArea);
		}

		if (showCpu())
		{
			auto cpur = GetCpuRect();
			cpur = offsetRect(cpur, { bounds_.left, bounds_.top });
			r = unionRect(r, cpur);
		}

		if(hasHoverScope())
		{
			auto scoperect = offsetRect(calcScopeRect(hoveredPin_.pinIndex), { bounds_.left, bounds_.top });
			r = unionRect(r, scoperect);
		}

		return r;
	}

	sharedGraphicResources_struct* ModuleViewStruct::getDrawingResources(gmpi::drawing::Factory& factory)
	{
		if(!drawingResources)
			drawingResources = drawingResourcesCache.get(factory);

		return drawingResources.get();
	}

	gmpi::drawing::Rect ModuleViewStruct::GetCpuRect()
	{
		gmpi::drawing::Rect r{0.f, 0.f, 101.f, 100.f};
		const float dx = (getWidth(bounds_) - getWidth(r)) * 0.5f;
		r = offsetRect(r, { dx, -12.f - getHeight(r) });
		return r;
	}

	// give the desired color, the opacity and the background color. Calc the brighter original color.
	Color calcColor(Color original, Color background, float opacity)
	{
		Color ret;
		ret.a = opacity;
		// original = x * opacity + (1-opacity) * background
		// (original -(1-opacity) * background) / opacity = x 
		// => x = (original - (1-opacity) * background) / opacity
		
		ret.r = (original.r - (1.0f - opacity) * background.r) / opacity;
		ret.g = (original.g - (1.0f - opacity) * background.g) / opacity;
		ret.b = (original.b - (1.0f - opacity) * background.b) / opacity;

		return ret;
	}

	Rect ModuleViewStruct::calcScopeRect(int pinIdx)
	{
		constexpr auto& plugDiameter = sharedGraphicResources_struct::plugDiameter;
		Rect scopeRect{ 0, 0, 48, scopeIsWave ? plugDiameter * 2.0f : plugDiameter};

		float y = 0.5f * plugDiameter;
		for (const auto& pin : plugs_)
		{
			if (pin.isVisible)
			{
				if (pin.indexCombined == hoveredPin_.pinIndex)
				{
					float w = 48.f;
					float h2 = scopeIsWave ? plugDiameter : plugDiameter * 0.6f;
					float x = pin.direction == DR_IN ? -w - 2 - plugDiameter : getWidth(bounds_) + 2 + plugDiameter;
					return
					{
						x,
						y - h2,
						x + w,
						y + h2
					};
					break;
				}

				y += plugDiameter;
			}
		}

		return {};
	}
	
	PathGeometry ModuleViewStruct::getOutline(gmpi::drawing::Factory drawingFactory)
	{
		if(!outlineGeometry)
			outlineGeometry = CreateModuleOutline(drawingFactory);

		return outlineGeometry;
	}

	void ModuleViewStruct::render(gmpi::drawing::Graphics& g)
	{
		constexpr auto& plugDiameter = sharedGraphicResources_struct::plugDiameter;

		// calc line thickness and offset to align nicely on pixel
		pixelSnapper2 snap(g.getTransform(), parent->drawingHost->getRasterizationScale());

#if 0 // debug layout and clip rects
		g.fillRectangle(getClipArea(),   g.createSolidColorBrush(Color::FromArgb(0x200000ff)));
		g.fillRectangle(getLayoutRect(), g.createSolidColorBrush(Color::FromArgb(0x2000ff00)));
#endif
        auto drawingFactory = g.getFactory();
        auto resources = getDrawingResources(drawingFactory);

		auto zoomFactor = g.getTransform()._11; // horizontal scale.
		resources->initializePinFillBrushes(g);

		// Cache outline.
		if (!outlineGeometry)
			outlineGeometry = CreateModuleOutline(drawingFactory);

		Brush backgroundBrush;// = &brush; // temp

		if (zoomFactor < 0.3f)
		{
			backgroundBrush = g.createSolidColorBrush(colorFromHex(0xE5E5E5u)); // todo: CACHE !!!!
		}
		else
		{
			if (muted)
			{
				backgroundBrush = g.createSolidColorBrush(Colors::DarkGray);
			}
			else
			{
				// SE 1.4
				//const auto GuiTopColor = Color::FromArgb(0xFFD2EFF2); // H185 S13 V94
				//const auto GuiBotColor = Color::FromArgb(0xFFB1C9CC); // V80
				constexpr float opacity = 0.85f;
#if 0 // slowish calculation
				// use original SE colors, but with some transparancy
				const Color greyBack{ 0xACACACu };
				const auto GuiTopColor = calcColor({ 0xDDEEFFu }, greyBack, opacity);//  Color::FromArgb(0xFFDDEEFF);
				const auto GuiBotColor = calcColor({ 0xAABBCCu }, greyBack, opacity);// Color::FromArgb(0xFFAABBCC);

				const auto DspTopColor = calcColor({ 0xEFEFEFu }, greyBack, opacity);// Color::FromArgb(0xFFEFEFEF); // S0
				const auto DspBotColor = calcColor({ 0xCCCCCCu }, greyBack, opacity);// Color::FromArgb(0xFFCCCCCC); // V80 S0
#else
				// calculated in advance (same formula)                          Greenish                                                   Blueish
				const Color GuiTopColor = isMonoDirectional() ? Color(0.777851462f, 1.103668900f, 0.933072031f, opacity) : Color(0.777851462f, 0.933072031f, 1.103668900f, opacity);
				const Color GuiBotColor = isMonoDirectional() ? Color(0.400113374f, 0.637583494f, 0.511825383f, opacity) : Color(0.400113374f, 0.511825383f, 0.637583494f, opacity);
				const Color DspTopColor(0.942677438f, 0.942677438f, 0.942677430f, opacity);
				const Color DspBotColor(0.637583494f, 0.637583494f, 0.637583494f, opacity);
#endif
				std::vector<gmpi::drawing::Gradientstop> gradientStops;

				auto totalHeight = getHeight(getLayoutRect());

				int plugCount = 0;
				int type = -1;
				for (auto& p : plugs_)
				{
					if (p.isVisible)
					{
						int t = p.isGuiPlug ? 0 : 1; // GUI or normal?

						if (t != type)
						{
							if (type == -1)
							{
								// Top color
								gradientStops.push_back({0.0f, t == 0 ? GuiTopColor : DspTopColor});
							}
							else
							{
								float fraction = (plugCount * plugDiameter) / totalHeight;
								if (t == 0)
								{
									gradientStops.push_back({fraction, interpolateColor(DspTopColor, DspBotColor, fraction)});
									gradientStops.push_back({fraction, interpolateColor(GuiTopColor, GuiBotColor, fraction)});
								}
								else
								{
									gradientStops.push_back({fraction, interpolateColor(GuiTopColor, GuiBotColor, fraction)});
									gradientStops.push_back({fraction, interpolateColor(DspTopColor, DspBotColor, fraction)});
								}
							}
							type = t;
						}
						++plugCount;
					}
				}
				// Bottom color
				gradientStops.push_back({1.0f, type == 0 ? GuiBotColor : DspBotColor});

				auto gradientStopCollection = g.createGradientstopCollection(gradientStops);
				LinearGradientBrushProperties lgbp1{ Point(0.f, 0.0f), Point(0.f, getHeight(bounds_)) };
				backgroundBrush = g.createLinearGradientBrush(lgbp1, BrushProperties(), gradientStopCollection);
			}
		}

		// Fancy outline.
		if ( zoomFactor > 0.25f)
		{
			g.fillGeometry(outlineGeometry, backgroundBrush);

			// snap the top corner to the pixel-grid. plus the stroke width, to get a crisp outline.
			const auto OutlineSpec = snap.thickness(isHovered_ ? 2.0f : 1.0f);
			const auto topLeftSnapped = snap.snapPixelOrigin({ 0.0f, 0.0f });

			const auto offsetY = topLeftSnapped.y + OutlineSpec.center_offset;
			const auto offsetX = topLeftSnapped.x + OutlineSpec.center_offset;
			const auto orig = g.getTransform();
			g.setTransform(makeTranslation(offsetX, offsetY) * orig);

			auto& moduleOutlineBrush = isHovered_ ? resources->moduleOutlineBrushHovered : resources->moduleOutlineBrush;

			g.drawGeometry(outlineGeometry, moduleOutlineBrush, OutlineSpec.width);

			g.setTransform(orig);

		}
		else
		{
			Rect r(0, 0, getWidth(bounds_), getHeight(bounds_));
			g.fillRectangle(r, backgroundBrush);
		}

		// Render plugin's shadow layer under module body
		if (pluginDrawingLayer_GMPI)
		{
			const auto transform = g.getTransform();
			auto adjustedTransform = makeTranslation(pluginGraphicsPos.left, pluginGraphicsPos.top) * transform;
			g.setTransform(adjustedTransform);

			auto gmpiContext = AccessPtr::get(g);
			pluginDrawingLayer_GMPI->renderLayer(gmpiContext, -1);

			g.setTransform(transform);
		}

		// Draw pin text elements.
		auto outlineBrush = g.createSolidColorBrush(Colors::Gray);
		if (zoomFactor > 0.1f)
		{
			const float pinRadius = 3.0f;
			const auto adjustedPinRadius = pinRadius + 0.1f; // nicer pixelation, more even outline circle.
			auto whiteBrush = g.createSolidColorBrush(Colors::White);

			// Pins (see also drawoutline for snapping)
			const float left = 0.0f;
			const float right = getWidth(getLayoutRect());

			Point p(0, plugDiameter * 0.5f);
			int pinIndex = 0;

			for (const auto& pin : plugs_)
			{
				if (pin.isVisible)
				{
					p.x = pin.direction == DR_IN ? left : right;

					// Spare container pins white.
					const int datatype = (pin.isAutoduplicatePlug && pin.isIoPlug) ? static_cast<int>(sharedGraphicResources_struct::pinColors.size()) - 1 : pin.datatype;

					const bool isPinCircleHovered = (hoveredPin_.pinIndex == pinIndex && hoveredPin_.hitCircle);
					auto& fillBrush = isPinCircleHovered ? resources->pinFillBrushesHovered[datatype] : resources->pinFillBrushes[datatype];
					auto& pinOutlineBrush = isPinCircleHovered ? resources->pinOutlineBrushesHovered[datatype] : resources->pinOutlineBrushes[datatype];

					g.fillCircle(p, adjustedPinRadius, fillBrush);

					if (zoomFactor > 0.25f)
					{
						// outline on plug circle
						// Unconected container pins highlighted white
						if (pin.isTiedToUnconnected)
							g.drawCircle(p, adjustedPinRadius, whiteBrush);
						else
							g.drawCircle(p, adjustedPinRadius, pinOutlineBrush);
					}

					p.y += plugDiameter;
				}
				++pinIndex;
			}
		}

		// Pin text and header text.
		if (zoomFactor > 0.5f)
		{
			// Text
			Rect r(0,0, getWidth(bounds_), getHeight(bounds_));
			r.top -= 0.5f;
			r.left  += static_cast<float>(plugTextHorizontalPadding + 0.5f * plugDiameter);
			r.right -= static_cast<float>(plugTextHorizontalPadding + 0.5f * plugDiameter);

			outlineBrush.setColor(Colors::Black);

			// Left justified text.
			g.drawTextU(lPlugNames, resources->tf_plugs_left, r, outlineBrush);

			// Right justified text.
			g.drawTextU(rPlugNames, resources->tf_plugs_right, r, outlineBrush);

			// Header.
			auto textExtraWidth = 1.0f + 0.5f * (std::max)(0.0f, resources->tf_header.getTextExtentU(name).width - getWidth(r));

			r.top -= 16;
			r.left -= textExtraWidth;
			r.right += textExtraWidth;
			g.drawTextU(name, resources->tf_header, r, outlineBrush);
		}

		// CPU graph and hover-scope are drawn in the layer-1 pass so they
		// overlay neighboring modules. See renderPluginLayer().

		// plugins own grphics
		if (pluginGraphics_GMPI)
		{
			// Transform to module-relative.
			const auto transform = g.getTransform();
			auto adjustedTransform = makeTranslation(pluginGraphicsPos.left, pluginGraphicsPos.top) * transform;

			g.setTransform(adjustedTransform);

			auto gmpiContext = AccessPtr::get(g);
			assert(gmpiContext);

			if (pluginDrawingLayer_GMPI)
			{
				// Layer-supporting plugins render layer 0 here.
				// Layers -1 and 1 are rendered by ViewBase in separate passes.
				pluginDrawingLayer_GMPI->renderLayer(gmpiContext, 0);
			}
			else
			{
				pluginGraphics_GMPI->render(gmpiContext);
			}

			g.setTransform(transform);
		}
		else if(pluginGraphics)
		{
			// Transform to module-relative.
			const auto transform = g.getTransform();
			auto adjustedTransform = makeTranslation(pluginGraphicsPos.left, pluginGraphicsPos.top) * transform;

			g.setTransform(adjustedTransform);

			// Render.
			pluginGraphics->OnRender(reinterpret_cast<GmpiDrawing_API::IMpDeviceContext*>(AccessPtr::get(g)));

			g.setTransform(transform);
		}

		// Hover-scope drawn in layer-1 pass (see renderPluginLayer) so it
		// overlays neighboring modules.

#if 0
		// check alignment
		//g.fillRectangle({ -1, -1, 1, 1 }, g.createSolidColorBrush(Colors::Red));

		auto br = g.createSolidColorBrush(Color(0.5f, 0.5f, 0.0, 0.5f));
		Rect localbounds(0, 0, getWidth(bounds_), getHeight(bounds_));
		g.drawRectangle(localbounds, br, 2.f);
#endif
	}

	bool ModuleViewStruct::hasRenderLayers() const
	{
		return pluginDrawingLayer_GMPI != nullptr || cpuInfo != nullptr || hasHoverScope();
	}

	void ModuleViewStruct::renderPluginLayer(Graphics& g, int32_t layer)
	{
		if (pluginDrawingLayer_GMPI && layer != -1)
		{
			const auto transform = g.getTransform();
			auto adjustedTransform = makeTranslation(pluginGraphicsPos.left, pluginGraphicsPos.top) * transform;
			g.setTransform(adjustedTransform);

			auto gmpiContext = AccessPtr::get(g);
			pluginDrawingLayer_GMPI->renderLayer(gmpiContext, layer);

			g.setTransform(transform);
		}

		// Diagnostic overlays draw on top of neighboring modules.
		if (layer == 1)
		{
			if (showCpu())
				RenderCpu(g);

			if (hasHoverScope())
				RenderHoverScope(g);
		}
	}

	void ModuleViewStruct::RenderCpu(Graphics& g)
	{
		const auto child_rect = GetCpuRect();
		g.pushAxisAlignedClip(child_rect);

		const auto rectBottom = child_rect.bottom;
		auto brush = g.createSolidColorBrush(gmpi::drawing::colorFromHex(0x00ff00u, 0.125f));
		g.fillRectangle(child_rect, brush);

		//		cpuInfo
		const float displayDecades = 4.0f; // 100% -> 0.01%
		const auto child_width = getWidth(child_rect);
		const auto child_height = getHeight(child_rect);
		auto penUG = g.createSolidColorBrush(gmpi::drawing::colorFromHex(0x00ff00u));
		auto bg = g.createSolidColorBrush(gmpi::drawing::colorFromHex(0x006400u));

		// BACKGROUND LINES
		auto textFormat = g.getFactory().createTextFormat(9.0f);
		textFormat.setTextAlignment(gmpi::drawing::TextAlignment::Right);

		const char* labels[] = {
			"100%",
			"10%",
			"1%",
			"0.1%",
			"",
			"",
		};

		// Graph horisontal grid lines.
		for(int i = (int)displayDecades - 1; i > 0; --i)
		{
			auto y = 0.5f + floorf(0.5f + child_rect.top + i * child_height / displayDecades);
			g.drawLine({ child_rect.left + 24, y }, { child_rect.right, y }, bg);
			g.drawTextU(labels[i], textFormat, Rect(child_rect.left + 2, y - 5, child_rect.left + 22, y + 7), bg);
		}

		// small dot for each voice status (off, suspended, sleep, on)
		{
			auto x = child_rect.left + 1;
			auto y = child_rect.top + 1;

			for(int i = 0; i < sizeof(cpuInfo->ModulesActive_); ++i)
			{
				Color colour;

				switch(cpuInfo->ModulesActive_[i])
				{
				case 1: //sleeping
					colour = Colors::Gray;
					break;

				case 2: // Suspended
					colour = Colors::Brown;
					break;

				case 3: //Run
					colour = Colors::Lime;
					break;

				default:
					i = 1000; // end, break the loop.
					continue;
					break;
				}

				gmpi::drawing::Rect r(x, y, x + 4, y + 4);
				penUG.setColor(colour);
				g.fillRectangle(r, penUG);
				x += 5;
				if(x > child_rect.right - 5)
				{
					x = child_rect.left + 1;
					y += 5;
				}
			}
		}

		// moving graph
		const float s = -child_height / displayDecades;
		int i = (cpuInfo->next_val + 1) % cpu_accumulator::CPU_HISTORY_COUNT;

		// Graphs.
		const float xinc = child_width / cpu_accumulator::CPU_HISTORY_COUNT;
		const auto graphSize = cpu_accumulator::CPU_HISTORY_COUNT;
		const float penWidth = 1;

		std::vector<gmpi::drawing::Point> plot;
		std::vector<gmpi::drawing::Point> plotSimplified;
		plot.reserve(cpu_accumulator::CPU_HISTORY_COUNT);
		plotSimplified.reserve(cpu_accumulator::CPU_HISTORY_COUNT);

		// PEAK.
		{
			float x = child_rect.left;
			const float* graph = cpuInfo->peaks;
			for(int k = 0; k < graphSize; ++k)
			{
				plot.push_back(
					{
						static_cast<float>(x),
						child_rect.top + graph[i] * s
					}
				);

				x += xinc;

				if(++i == cpu_accumulator::CPU_HISTORY_COUNT) // wrap.
				{
					i = 0;
				}
			}

			SimplifyGraph(plot, plotSimplified);

			auto geometry = DataToGraph(g, plotSimplified);

			penUG.setColor(Colors::Gray);
			g.drawGeometry(geometry, penUG, penWidth);
		}

		// AVERAGE.
		{
			plot.clear();
			plotSimplified.clear();

			float x = child_rect.left;
			const float* graph = cpuInfo->values;
			for(int k = 0; k < graphSize; ++k)
			{
				plot.push_back(
					{
						static_cast<float>(x),
						child_rect.top + graph[i] * s
					}
				);

				x += xinc;

				if(++i == cpu_accumulator::CPU_HISTORY_COUNT) // wrap.
				{
					i = 0;
				}
			}

			SimplifyGraph(plot, plotSimplified);

			auto geometry = DataToGraph(g, plotSimplified);

			penUG.setColor(Colors::White);
			g.drawGeometry(geometry, penUG, penWidth);
		}

		// percent printout
		std::ostringstream oss;
		oss << setiosflags(ios_base::fixed) << setprecision(4) << cpuInfo->cpuRunningMedianSlow * 100.0f << " %";
		bg.setColor(Colors::Black);
		g.drawTextU(oss.str().c_str(), textFormat, Rect(child_rect.right - 40, rectBottom, child_rect.right, rectBottom - 12), bg);

		g.popAxisAlignedClip();
	}

	void ModuleViewStruct::RenderHoverScope(Graphics& g)
	{
		const auto scopeRect = calcScopeRect(hoveredPin_.pinIndex);

		auto brush = g.createSolidColorBrush(Color(0, 0, 0.0f, 0.4f));
		g.fillRoundedRectangle({ scopeRect, 3.0f }, brush);

		if (scopeIsWave)
		{
			// axis line
			const float midY = scopeRect.top + getHeight(scopeRect) * 0.5f;
			brush.setColor(Color(0, 0, 0.0f, 0.7f));
			g.drawLine({ scopeRect.left, midY }, { scopeRect.right, midY }, brush, 0.5f);

			auto geometry = g.getFactory().createPathGeometry();
			auto sink = geometry.open();

			constexpr int numPoints = 192;
			const float yScale = getHeight(scopeRect) * 0.5f;
			const float yMiddle = scopeRect.top + yScale;
			const float dx = getWidth(scopeRect) / numPoints;
			constexpr float offEdgeDips = 1.f;
			const auto offEdge = static_cast<int>(std::round(offEdgeDips / dx)); // to hide the vertical outline on the left and right.
			Point p{ scopeRect.left - offEdgeDips, 0.f };
			const int indexMask = static_cast<int>(std::size(movingPeaks)) - 1;

			// top line
			bool begun{};
			int i = 0;
			for (; i < (2 * offEdge + numPoints) * 2; i += 2) // we're drawing 0.5 pixels off each side to hide the vertical line.
			{
				int j = indexMask & (movingPeaksIdx + i);

				if (movingPeaks[j] > -90.0f)
				{
					p.y = yMiddle - yScale * movingPeaks[j];
					if (!begun)
					{
						sink.beginFigure(p, FigureBegin::Filled);
						begun = true;
					}
					else
					{
						sink.addLine(p);
					}
				}

				p.x += dx;
			}

			if (begun)
			{
				// bottom line (iterate forward through odd indices)
				bool first = true;
				i -= 3; // now we're accessing the odd indexes, which are the minimum values.
				for (; i > 0; i -= 2)
				{
					int j = indexMask & (movingPeaksIdx + i);

					if (movingPeaks[j] <= -90.0f)
						break;

					p.x -= dx;
					p.y = yMiddle - yScale * movingPeaks[j];
					sink.addLine(p);
				}

				sink.endFigure(FigureEnd::Closed);
				sink.close();

				g.pushAxisAlignedClip(scopeRect);

				brush.setColor(colorFromHex(0x00FF00u, 0.5f));
				g.fillGeometry(geometry, brush);
				brush.setColor(Colors::Lime);

				StrokeStyleProperties ssp{ CapStyle::Flat, LineJoin::Bevel }; // CapStyle removes 'hairy' spikes on high freqs.
				auto sstyle = g.getFactory().createStrokeStyle(ssp);

				g.drawGeometry(geometry, brush, 1.0f, sstyle);

				g.popAxisAlignedClip();
			}
		}
		else
		{
			const auto& pin = plugs_[hoveredPin_.pinIndex];

			// numeric data is sized on cap-height, textual on body-height.
			FontFlags flags = FontFlags::CapHeight;

			switch(pin.datatype)
			{
			case DT_TEXT:
			case DT_STRING_UTF8:
			case DT_BOOL:
			case DT_ENUM:
				flags = FontFlags::BodyHeight;
				break;
			default:
				break;
			}

			auto font = g.getFactory().createTextFormat(
				9.0f
				, {}
				, FontWeight::Regular
				, FontStyle::Normal
				, FontStretch::Normal
				, flags
			);

			const auto metrics = font.getFontMetrics();

			font.setWordWrapping(WordWrapping::NoWrap);

			// center text nicely vertially
			const auto baseLine = scopeRect.top + metrics.ascent;
			const auto capTop = baseLine - metrics.capHeight;
			const auto roomAtTop = capTop - scopeRect.top;
			const auto roomBelowBaseline = scopeRect.bottom - baseLine;

			const auto yAdjust = (roomBelowBaseline - roomAtTop) * 0.5f;

			auto centeredTextRect = offsetRect(scopeRect, { 0, yAdjust });
			centeredTextRect.left += 2;
			centeredTextRect.right -= 2;

			brush.setColor(Colors::Yellow);
			g.drawTextU(hoverScopeText.c_str(), font, centeredTextRect, brush, DrawTextOptions::Clip);
		}
	}

	int32_t ModuleViewStruct::setPin(ModuleView* fromModule, int32_t fromPinIndex, int32_t pinIndex, int32_t voice, int32_t size, const void* data)
	{
		if (editorPinValues)
		{
			auto& vals = *editorPinValues.get();
			vals[pinIndex].assign((uint8_t*)data, size + (uint8_t*)data);

			if (pinIndex == hoveredPin_.guiPin && !hoveredPin_.hitCircle)
			{
				const auto& pin = plugs_[hoveredPin_.pinIndex];
				hoverScopeText = NiceFormatted(vals[pinIndex], (EPlugDataType)pin.datatype);
				invalidateMyRect(calcScopeRect(hoveredPin_.pinIndex));
			}
		}

		return ModuleView::setPin(fromModule, fromPinIndex, pinIndex, voice, size, data);
	}

	int32_t ModuleViewStruct::pinTransmit(int32_t pinIndex, int32_t size, const void* data, int32_t voice)
	{
		if (editorPinValues)
		{
			auto& vals = *editorPinValues.get();
			vals[pinIndex].assign((uint8_t*)data, size + (uint8_t*)data);

			if (pinIndex == hoveredPin_.guiPin && !hoveredPin_.hitCircle)
			{
				const auto& pin = plugs_[hoveredPin_.pinIndex];
				hoverScopeText = NiceFormatted(vals[pinIndex], (EPlugDataType)pin.datatype);
				invalidateMyRect(calcScopeRect(hoveredPin_.pinIndex));
			}
		}

	   return ModuleView::pinTransmit(pinIndex, size, data, voice);
	}

	bool ModuleViewStruct::hasHoverScope() const
	{
		return hoveredPin_.pinIndex > -1 && (scopeIsWave || !hoverScopeText.empty());
	}

	void ModuleViewStruct::SetHoverScopeText(const char* text)
	{
		hoverScopeText = text;

		if(hasHoverScope())
		{
			const auto& pin = plugs_[hoveredPin_.pinIndex];

			invalidateMyRect(calcScopeRect(hoveredPin_.pinIndex));
		}
		scopeIsWave = false;
	}

	void ModuleViewStruct::SetHoverScopeWaveform(std::unique_ptr< std::vector<float> > data)
	{
		movingPeaks[movingPeaksIdx++] = (*data)[0];
		movingPeaks[movingPeaksIdx++] = (*data)[1];

		movingPeaksIdx &= (std::size(movingPeaks) - 1);

		auto scopeRect = calcScopeRect(hoveredPin_.pinIndex);
		invalidateMyRect(scopeRect);

		scopeIsWave = true;
	}

	PathGeometry ModuleViewStruct::CreateModuleOutline(Factory& factory)
	{
		constexpr auto plugDiameter = sharedGraphicResources_struct::plugDiameter;

		vector<pinViewInfo> filteredChildren;
		for (auto& pin : plugs_)
		{
			if (!pin.isVisible)
				continue;

			filteredChildren.push_back(pin);
		}

		// Create plugnames
		string lPlugNamesTemp;
		string rPlugNamesTemp;
		rPlugNames.clear();

		bool first = true;

		for (const auto& pin : filteredChildren)
		{
			if (!first)
			{
				lPlugNamesTemp.append("\n");
				rPlugNamesTemp.append("\n");
			}
			else
			{
				first = false;
			}

			if (pin.direction == DR_IN)
				lPlugNamesTemp.append(pin.name);
			else
				rPlugNamesTemp.append(pin.name);
		}

		// Add entry for embedded graphics.
		float clientHight = 0.0f;
		if (pluginGraphics_GMPI || pluginGraphics)
		{
			clientHight = getHeight(pluginGraphicsPos);
			if (clientHight > 0.0f)
				clientHight += clientPadding * 2.0f;
		}

		// or if no plugs, place dummy object in list to prevent problems creating outline
		if( filteredChildren.empty() && clientHight == 0.0f)
			clientHight = plugDiameter;

		if (clientHight != 0.0f)
		{
			pinViewInfo info{};
			info.name = "GFX";
			info.direction = -1;
			info.datatype = -1;
			info.indexCombined = -1;
			info.isGuiPlug = false;
			info.isIoPlug = false;
			info.isVisible = true;
			info.plugDescID = -1;

			filteredChildren.push_back(info);
		}

		rPlugNames = rPlugNamesTemp;
		lPlugNames = lPlugNamesTemp;

		// outline cache currently stores legacy path objects, skip cache lookup here.
		// cache copies of the same shape
		std::string outlineSpecification;
		{
			for(const auto& vc : filteredChildren)
			{
				if(vc.direction == -1) // indicates embedded gfx, not pin.
				{
					outlineSpecification += std::to_string(clientHight); // include client height in spec
				}
				else
				{
					outlineSpecification += vc.direction == DR_IN ? 'B' : 'F';
				}
			}
			outlineSpecification += ':' + std::to_string(getWidth(bounds_)); // add width to specification

			auto& outlineCache = getDrawingResources(factory)->outlineCache;
			if(auto it = outlineCache.find(outlineSpecification); it != outlineCache.end())
			{
//				it->second->addRef();
//				[[maybe_unused]] auto refcount = it->second->release();

				//				_RPT1(_CRT_WARN, "Saved one! refCount %d\n", refcount);

				return it->second;
			}
		}

		auto geometry = factory.createPathGeometry();
		auto sink = geometry.open();

		const auto localBounds = Rect{ 0, 0, getWidth(bounds_), getHeight(bounds_) };
		const float radius = plugDiameter * 0.5f;

		// left and right borders, when flat.
		const float leftX = localBounds.left; // radius - 0.5f;
		// we can't draw past right outline.
		const float rightX = localBounds.right; //(bounds_.right - bounds_.left) - radius /*- outlineThickness*/ +0.5f;

		// Half-circles. The BEST 2-spline magic number is 1.333333
		const float controlPointDistance = radius * 1.333f;
		// Quarter-circles.
		const float QCPDistance = radius * 0.551784f;

		typedef int plugType;

#if 1 //def _DEBUG
		const bool NEW_LOOK_CURVES = true;
#else
		const bool NEW_LOOK_CURVES = false;
#endif

		const float diameterBig = plugDiameter;
		const float radiusBig = diameterBig * 0.5f;
		const float radiusSmall = radiusBig * 0.2f;

		const float smallCurveXCenter = sqrtf((radiusBig + radiusSmall) * (radiusBig + radiusSmall) - radiusBig * radiusBig );
		const float smallCurveXIntersect = smallCurveXCenter * radiusBig / (radiusBig + radiusSmall);
		float smallCurveYIntersect = radiusBig * radiusSmall / (radiusBig + radiusSmall);
		const gmpi::drawing::Size bigCurveSize(radiusBig, radiusBig);
		const gmpi::drawing::Size smallCurveSize(radiusSmall, radiusSmall);

		float childHeight = 0;

		int idx = 0;
		int childCount = (int)filteredChildren.size();

		// Pin coloring.
		bool startedFigure = false;

		enum { EBump, EFlat, EPlugingraphics };

		int edgeType = EFlat;
		int prevEdgeType = EFlat;
		float edgeX = leftX;
		float y = 0.0f; // why? -0.5;

		// Down left side
		for (const auto& vc : filteredChildren)
		{
			bool isFirst = idx == 0; // 1;
			bool isLast = idx == childCount - 1;

			if (vc.direction == -1) // indicates embedded gfx, not pin.
			{
				childHeight = clientHight;
				edgeType = EPlugingraphics;
			}
			else
			{
				childHeight = plugDiameter;
				edgeType = vc.direction == DR_IN ? EBump : EFlat;
			}

			if (isFirst)
			{
				switch (edgeType)
				{
				case EBump: // start point at top-left.
				case EPlugingraphics:
					sink.beginFigure(edgeX, y, FigureBegin::Filled);
					break;

				case EFlat: // small curve at top-left.
					sink.beginFigure(gmpi::drawing::Point(edgeX + radius, y), FigureBegin::Filled);
					BezierSegment bs1(gmpi::drawing::Point(edgeX + radius - QCPDistance, y), gmpi::drawing::Point(edgeX, y + radius - QCPDistance), gmpi::drawing::Point(edgeX, y + radius));
					sink.addBezier(bs1);
					break;
				}
			}

			// bump on left.
			if (edgeType == EBump)
			{
				if (!isFirst)
				{
					if (prevEdgeType == EBump)
					{
						// Draw inner curve, from previous bump.
						sink.addArc(
							ArcSegment(gmpi::drawing::Point(edgeX - smallCurveXIntersect, y + smallCurveYIntersect), smallCurveSize));
					}
					else
					{
						// Draw line down,then out to meet curve.
						sink.addLine(gmpi::drawing::Point(edgeX, y - radiusSmall));
						sink.addLine(gmpi::drawing::Point(edgeX - smallCurveXIntersect, y + smallCurveYIntersect));
					}
				}

				Point endPoint;
				if (isLast) // curve goes all the way to bottom of plug.
				{
					endPoint = Point(edgeX, y + childHeight);
				}
				else
				{
					// Curve goes as far as next inner-curve between bumps.
					endPoint = Point(edgeX - smallCurveXIntersect, y + childHeight - smallCurveYIntersect);
				}

				ArcSegment as1(endPoint, bigCurveSize, 0.0, SweepDirection::CounterClockwise);
				sink.addArc(as1);
			}
			else // Flat on left
			{
				if (prevEdgeType == EBump)
				{
					// Angled line from Curve to left flat edge.
					sink.addLine(gmpi::drawing::Point(edgeX, y + radiusSmall));
				}

				// Bottom-left corner.
				if (isLast)
				{
					if (edgeType == EPlugingraphics) // sharp corner.
					{
						sink.addLine(gmpi::drawing::Point(edgeX, y + childHeight)); // nope ruins pixel snap +1.0f)); // +1 hack to lower bottom edge of scope etc
					}
					else
					{
						// small curve.
						sink.addLine(gmpi::drawing::Point(edgeX, y + radius));
						BezierSegment bs2(gmpi::drawing::Point(edgeX, y + radius + QCPDistance), gmpi::drawing::Point(edgeX + radius - QCPDistance, y + childHeight), gmpi::drawing::Point(edgeX + radius, y + childHeight));
						sink.addBezier(bs2);
					}
				}
			}

			if (isLast) // bottom edge
			{
				switch (edgeType)
				{
				case EPlugingraphics:
					// Draw square bottom-right.
					sink.addLine(gmpi::drawing::Point(rightX, y + childHeight)); // try to quantizr bottom line to pixel grid    +1.0f));
					break;

				case EFlat:
					// don't draw all way to edge.
					sink.addLine(gmpi::drawing::Point(rightX, y + childHeight));
					break;

				case EBump:
					// don't draw all way to edge to allow for curved bottom-right.
					sink.addLine(gmpi::drawing::Point(rightX - radius, y + childHeight));
					break;
				};
			}
			else
			{
				y += plugDiameter;
				idx++;
			}

			prevEdgeType = edgeType;
		}

		// right side.
		edgeX = rightX;
		y += childHeight;

		// up right side.
		for( auto it = filteredChildren.rbegin() ; it != filteredChildren.rend() ; ++it)
		{
			auto& vc = (*it);

			bool isFirst = idx == 0;
			bool isLast = idx == childCount - 1;

			if (vc.direction == -1) // indicates embedded gfx, not pin.
			{
				childHeight = clientHight;
				edgeType = EPlugingraphics;
			}
			else
			{
				childHeight = plugDiameter;
				edgeType = vc.direction == DR_OUT ? EBump : EFlat;
//edgeType = EFlat;
			}

			y -= childHeight;

			// bump on right.
			if (edgeType == EBump)
			{
				if (!isLast)
				{
					if (prevEdgeType == EBump)
					{
						// Draw inner curve, from previous bump.
						sink.addArc(
							ArcSegment(gmpi::drawing::Point(edgeX + smallCurveXIntersect, y + childHeight - smallCurveYIntersect), smallCurveSize));
					}
					else
					{
						// Draw line up,then out to meet curve.
						sink.addLine(gmpi::drawing::Point(edgeX, y + childHeight + radiusSmall));
						sink.addLine(gmpi::drawing::Point(edgeX + smallCurveXIntersect, y + childHeight - smallCurveYIntersect));
					}
				}

				Point endPoint;
				if (isFirst) // curve goes all the way to top of plug.
				{
					endPoint = Point(edgeX, y);
				}
				else
				{
					endPoint = Point(edgeX + smallCurveXIntersect, y + smallCurveYIntersect);
				}

				ArcSegment as1(endPoint, bigCurveSize, 0.0, SweepDirection::CounterClockwise);
				sink.addArc(as1);
			}
			else // flat on right.
			{
				if (isLast)
				{
					if (edgeType != EPlugingraphics) // non sharp corner.
					{
						// The BEST 4-spline magic number is 0.551784. (not used yet).
						BezierSegment bs5(gmpi::drawing::Point(edgeX - radius + QCPDistance, y + childHeight), gmpi::drawing::Point(edgeX, y + radius + QCPDistance), gmpi::drawing::Point(edgeX, y + radius));
						sink.addBezier(bs5);
					}
				}
				else
				{
					if (prevEdgeType == EBump)
					{
						// Angled line from Curve to left flat edge.
						sink.addLine(gmpi::drawing::Point(edgeX, y + childHeight - radiusSmall));
					}
				}

				// Top-right corner.
				if (isFirst)
				{
					if (edgeType == EPlugingraphics) // sharp corner.
					{
						sink.addLine(gmpi::drawing::Point(edgeX, y));
					}
					else
					{
						// small curve.
						sink.addLine(gmpi::drawing::Point(edgeX, y + radius));

						BezierSegment bs6(gmpi::drawing::Point(edgeX, y + radius - QCPDistance), gmpi::drawing::Point(edgeX - radius + QCPDistance, y), gmpi::drawing::Point(edgeX - radius, y));
						sink.addBezier(bs6);
					}
				}
			}

			idx--;
			prevEdgeType = edgeType;
		}

		sink.endFigure();
		sink.close();

		getDrawingResources(factory)->outlineCache[outlineSpecification] = geometry;

		return geometry;
	}

	int ModuleViewStruct::getPinDatatype(int pinIndex)
	{
		if (pinIndex < plugs_.size())
			return plugs_[pinIndex].datatype;

		return 0;
	}
	bool ModuleViewStruct::getPinGuiType(int pinIndex)
	{
		if (pinIndex < plugs_.size())
			return plugs_[pinIndex].isGuiPlug;

		return false;
	}

	gmpi::drawing::Point ModuleViewStruct::getConnectionPoint(CableType cableType, int pinIndex)
	{
		constexpr auto plugDiameter = sharedGraphicResources_struct::plugDiameter;

		if (cableType == CableType::PatchCable)
			return ModuleView::getConnectionPoint(cableType, pinIndex);

		int i = 0;
		float y = bounds_.top + plugDiameter * 0.5f;// -0.5f;
		for (auto& p : plugs_)
		{
			if (p.isVisible)
			{
				if (i == pinIndex)
				{
					float x = p.direction == DR_OUT ? bounds_.right/* - plugDiameter * 0.5f + 0.5f*/ : bounds_.left/* + plugDiameter * 0.5f - 0.5f*/;
					//				_RPT2(_CRT_WARN, "getConnectionPoint [%.3f,%.3f]\n", x, y);
					return Point(x, y);
				}
				y += plugDiameter;
			}
			++i;
		}

		// Complete failure.
		return gmpi::drawing::Point(bounds_.right, y);
	}

	float ModuleViewStruct::hitTestFuzzy(int32_t flags, gmpi::drawing::Point point)
	{
		constexpr float fuzzyLimit = 12.f;
		constexpr float solidHit = 0.f;
		constexpr float totalMiss = 1000.f;

		if (!isVisable())
		{
			return totalMiss;
		}

		const auto r = getLayoutRect();
		auto r2 = r;
		r2 = inflateRect(r2, fuzzyLimit);

		r2.top -= 16.0f; // allow for title bar.

		if(!pointInRect(point, r2)) // weed out clear misses fast.
		{
			return totalMiss;
		}

		// client area is always a hit, even when adorner active.
		{
			const gmpi::drawing::Point localPoint{ point.x - bounds_.left, point.y - bounds_.top };
			if(pointInRect(localPoint, pluginGraphicsPos))
			{
				return solidHit;
			}
		}

		// hits solidly within outline are good.
		if(pointInRect(point, r))
		{
			return solidHit;
		}

		float best = totalMiss;
		auto pin = getPinUnderMouse(point);

		if(pin.pinIndex > -1)
			best = pin.distance;

		// return distance to outline
		const auto distanceToOutline = max(max(r.left - point.x, point.x - r.right), max(r.top - point.y, point.y - r.bottom));
		best = (std::min)(best, distanceToOutline);

		return best;
	}

	// Return pin under mouse and hit details.
	// if we hit the circle we create a new line, the text - highlight the lines connected to that pin.
	pinHit ModuleViewStruct::getPinUnderMouse(gmpi::drawing::Point point)
	{
	//	_RPT2(_CRT_WARN, "getPinUnderMouse: point=[%.2f,%.2f]\n", point.x, point.y);

		constexpr auto plugDiameter = sharedGraphicResources_struct::plugDiameter;

		const float pinHitRadiusSquared = plugDiameter * plugDiameter; // twice drawn size.

		const float left = getLayoutRect().left + plugDiameter * 0.5f;
		const float right = getLayoutRect().right - plugDiameter * 0.5f;

		Point p(0, getLayoutRect().top + plugDiameter * 0.5f - 0.5f);

		float outerLimitSquared = (fuzzyHitTestLimit + plugDiameter * 0.5f) * (fuzzyHitTestLimit + plugDiameter * 0.5f);
		pinHit closestPin{ -1, -1, -1, -1, outerLimitSquared, true };
		Rect pinRect{ left, getLayoutRect().top, right, getLayoutRect().top + static_cast<float>(plugDiameter) };
		float plugHitWidth = (std::min)(40.0f, right-left); // width of area responsive to clicking on plug in general (not connection point).

		int guiIndex = -1;
		int dspIndex = -1;
		for (const auto& pin : plugs_)
		{
			guiIndex += pin.isGuiPlug ? 1 : 0;
			dspIndex += !pin.isGuiPlug ? 1 : 0;

			if(!pin.isVisible)
				continue;

			if (pin.direction == DR_IN)
			{
				p.x = left;
				pinRect.left = left + plugDiameter;
				pinRect.right = left + plugHitWidth;
			}
			else
			{
				p.x = right;
				pinRect.right = right - plugDiameter;
				pinRect.left = right - plugHitWidth;
			}

			// Click on plug in general (but not on connection point).
			if(pointInRect(point, pinRect))
			{
				closestPin.pinIndex = pin.indexCombined;
				closestPin.pinID = pin.plugDescID;
				closestPin.guiPin = pin.isGuiPlug ? guiIndex : -1;
				closestPin.dspPin = pin.isGuiPlug ? -1 : dspIndex;
				closestPin.distance = 0.0f;
				closestPin.hitCircle = false;
//				_RPT2(_CRT_WARN, "  -> Hit pin text area: pinIndex=%d, pinID=%d\n", closestPin.pinIndex, closestPin.pinID);
				return closestPin;
			}

			float distanceSquared = (point.x - p.x) * (point.x - p.x) + (point.y - p.y) * (point.y - p.y);

			if(distanceSquared < closestPin.distance)
			{
				closestPin.pinIndex = pin.indexCombined;
				closestPin.pinID = pin.plugDescID;
				closestPin.guiPin = pin.isGuiPlug ? guiIndex : -1;
				closestPin.dspPin = pin.isGuiPlug ? -1 : dspIndex;
				closestPin.distance = distanceSquared;
			}

			p.y += plugDiameter;
			pinRect.top += plugDiameter;
			pinRect.bottom += plugDiameter;
		}

		closestPin.distance = std::max(0.0f, sqrtf(closestPin.distance) - plugDiameter * 0.5f);

//		_RPT4(_CRT_WARN, "  -> pinIndex=%d, pinID=%d, distance=%.2f, hitCircle=%d\n", 
	//		closestPin.pinIndex, closestPin.pinID, closestPin.distance, closestPin.hitCircle);
		return closestPin;
	}

	int32_t ModuleViewStruct::OnDoubleClicked(gmpi::drawing::Point point, int32_t flags)
	{
		return Presenter()->OnCommand(PresenterCommand::Open, getModuleHandle());
	}

	gmpi::ReturnCode ModuleViewStruct::onPointerDown(gmpi::drawing::Point point, int32_t flags)
	{
		auto res = ModuleView::onPointerDown(point, flags);

		if(gmpi::ReturnCode::Ok == res || gmpi::ReturnCode::Handled == res) // Client was hit (and cared).
			return res;

		if ((flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0)
		{
			// Handle double-click on module. (only if didn't click on a child control)
			auto now = std::chrono::steady_clock::now();
			auto timeSincePreviousClick = now - lastClickedTime;
			auto timeSincePreviousClick_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeSincePreviousClick).count();
			lastClickedTime = now;

#if defined( _WIN32)
			const int doubleClickThreshold_ms = GetDoubleClickTime();
#else
			const int doubleClickThreshold_ms = 500;
#endif
			if (timeSincePreviousClick_ms < doubleClickThreshold_ms)
			{
				auto res2 = (gmpi::ReturnCode) OnDoubleClicked(point, flags);

				if (gmpi::ReturnCode::Handled == res2)
					return res2;
			}

			// handle click on pins
			auto toPin = getPinUnderMouse(point);
			if (toPin.pinIndex >= 0)
			{
				if (toPin.hitCircle) // Hit connection circle.
				{
					// pickup existing cable
					const auto isAltHeld = 0 != (flags & gmpi_gui_api::GG_POINTER_KEY_ALT);
					if(isAltHeld)
					{
						// find existing cable
						if(auto [cable, whichend] = parent->getTopCable(handle, toPin.pinIndex); cable)
						{
							Presenter()->OnCommand(
								whichend == 1 ? PresenterCommand::PickupLineFrom : PresenterCommand::PickupLineTo
								, cable->handle
							);

							return gmpi::ReturnCode::Ok;
						}
					}

					// start a new cable
					auto dragStartPoint = getConnectionPoint(CableType::StructureCable, toPin.pinIndex);
					parent->StartCableDrag(this, toPin.pinIndex, dragStartPoint, point);
				}
				else // hit text. wait and see if we dragged the module. before tracing wire.
				{
					boundsOnMouseDown = bounds_;
				}
			}
		}

		return ((flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled; // left-click: Indicate need for drag.
	}

	void ModuleViewStruct::OnClickedButDidntDrag()
	{
		if(hoveredPin_.pinIndex > -1 && !hoveredPin_.hitCircle)
			Presenter()->HighlightConnector(this->handle, hoveredPin_.pinIndex, PinHighlightFlag_Emphasise);
	}

	gmpi::ReturnCode ModuleViewStruct::onPointerMove(gmpi::drawing::Point point, int32_t flags)
	{
		if (!mouseCaptured)
		{
			auto newHoveredPin = getPinUnderMouse(point);

			if (hoveredPin_.pinIndex != newHoveredPin.pinIndex || hoveredPin_.hitCircle != newHoveredPin.hitCircle)
			{
				// temporarily trace, only while highlighted
				if(hoveredPin_.pinIndex > -1)
				{
					Presenter()->HighlightConnector(this->handle, hoveredPin_.pinIndex, ~PinHighlightFlag_EmphasiseMomentary);

//	hoverScopeWaveform can be null if you turn off audio while scope displayed, still needs erasing but				if(hoverScopeWaveform || !hoverScopeText.empty())
					{
						invalidateMyRect(calcScopeRect(hoveredPin_.pinIndex));
					}
				}

				if(newHoveredPin.pinIndex > -1)
					Presenter()->HighlightConnector(this->handle, newHoveredPin.pinIndex, PinHighlightFlag_EmphasiseMomentary);

				hoveredPin_ = newHoveredPin;
				hoverScopeWaveform = {};
				scopeIsWave = false;
				hoverScopeText.clear();
				std::fill(std::begin(movingPeaks), std::end(movingPeaks), -99.0f);

				int dspHoverPin = hoveredPin_.hitCircle ? -1 : hoveredPin_.pinIndex;

				if (editorPinValues && hoveredPin_.guiPin >= 0 && !hoveredPin_.hitCircle)
				{
					auto& vals = *editorPinValues.get();
					auto& raw = vals[hoveredPin_.guiPin];

					dspHoverPin = -1; // CUG has nothing to do.
					hoverScopeText = NiceFormatted(raw, (EPlugDataType) plugs_[hoveredPin_.pinIndex].datatype);
				}
				Presenter()->setHoverScopePin(handle, dspHoverPin);
				parent->ChildInvalidateRect(getClipArea());
			}
		}
		else
		{
			/* todo
			// if the intention was not to drag the module, then a click on text should highlight the current pin.
			if(bounds_ == boundsOnMouseDown && )
				Presenter()->HighlightConnector(this->handle, toPin.pinIndex, PinHighlightFlag_Emphasise);
			*/
		}

		ModuleView::onPointerMove(point, flags);
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ModuleViewStruct::setHover(bool mouseIsOverMe)
	{
		bool visualStateChanged = isHovered_ != mouseIsOverMe;

		auto r = ModuleView::setHover(mouseIsOverMe);

		if (!mouseIsOverMe && hoveredPin_.pinIndex != -1)
		{
			Presenter()->HighlightConnector(this->handle, hoveredPin_.pinIndex, ~PinHighlightFlag_EmphasiseMomentary);

			invalidateMyRect(calcScopeRect(hoveredPin_.pinIndex));

			hoveredPin_ = { -1, -1, -1, -1, 0.0f, true };
			hoverScopeWaveform = {};
			scopeIsWave = false;
			hoverScopeText.clear();
			Presenter()->setHoverScopePin(handle, -1);
		}

		if (visualStateChanged)
			parent->ChildInvalidateRect(getClipArea());

		return r;
	}

	void ModuleViewStruct::OnCableDrag(ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistanceSquared, ModuleView*& bestModule, int& bestPinIndex)
	{
		constexpr auto plugDiameter = sharedGraphicResources_struct::plugDiameter;

		if (dragline->type == CableType::StructureCable)
		{
			auto point = dragPoint;

			const float pinHitRadius = bestDistanceSquared;

			// rough hit-test on enlarged layout rect.
			auto r = getLayoutRect();
			r = inflateRect(r, pinHitRadius);
			if (!pointInRect(point, r))
			{
				return;
			}

			const float left = getLayoutRect().left + plugDiameter * 0.5f;
			const float right = getLayoutRect().right - plugDiameter * 0.5f;

			Point p(0, getLayoutRect().top + plugDiameter * 0.5f - 0.5f);

			for (const auto& pin : plugs_)
			{
				if (pin.isVisible)
				{
					if (pin.direction == DR_IN)
					{
						p.x = left;
					}
					else
					{
						p.x = right;
					}

					float distanceSquared = (point.x - p.x) * (point.x - p.x) + (point.y - p.y) * (point.y - p.y);
					if (distanceSquared < bestDistanceSquared && Presenter()->CanConnect(dragline->type, dragline->fixedEndModule(), dragline->fixedEndPin(), handle, pin.indexCombined))
					{
						bestDistanceSquared = distanceSquared;
						bestPinIndex = pin.indexCombined;
						bestModule = this;
					}

					p.y += plugDiameter;
				}
			}
		}
		else
		{
			ModuleView::OnCableDrag(dragline, dragPoint, bestDistanceSquared, bestModule, bestPinIndex);
		}
	}

	// ignores fuzzy hit testing of pins, but since the line will have snapped to the exact center of the correct pin, should work out OK.
	bool ModuleViewStruct::EndCableDrag(gmpi::drawing::Point unused, ConnectorViewBase* dragline, int32_t keyFlags)
	{
		auto hitPin = getPinUnderMouse(dragline->dragPoint());

		if (dragline->type != CableType::StructureCable || hitPin.pinIndex < 0 || !hitPin.hitCircle)
			return false;

		// did we drag a line back to it's original pin and <ALT>-click?
		if((keyFlags & gmpi_gui_api::GG_POINTER_KEY_ALT) != 0 && hitPin.pinIndex == dragline->dragEnd().index && getModuleHandle() == dragline->dragEnd().module)
		{
			// PROB: dragline no longer holds old connection info, since it's a fresh one.
			int x = 9;
		}

		// redraw the line as though it were a normal connection, to give immediate visual feedback to the user.
		const auto fixedEnd = dragline->fixedEnd();
		dragline->draggingFromEnd = -1;
		dragline->parent->ChildInvalidateRect(dragline->bounds_);

		return Presenter()->AddConnector(fixedEnd.module, fixedEnd.index, getModuleHandle(), hitPin.pinIndex, false);
	}

	std::unique_ptr<IViewChild> ModuleViewStruct::createAdorner(ViewBase* pParent)
	{
		return std::make_unique<ResizeAdornerStructure>(pParent, this);
	}

	void ModuleViewStruct::OnCpuUpdate(cpu_accumulator* pCpuInfo)
	{
		auto r = offsetRect(GetCpuRect(), { bounds_.left, bounds_.top });
		parent->ChildInvalidateRect(r);

		cpuInfo = pCpuInfo;
	}

#if 0 // TODO
	void ModuleViewStruct::invalidateMeasure()
	{
		if (!initialised_)
			return;

		gmpi::drawing::Size current{ getWidth(bounds_), getHeight(bounds_) };
        gmpi::drawing::Size desired{};
        measure(current, &desired);

        if (current != desired)
        {
			parent->Presenter()->OnCommand(PresenterCommand::RefreshView);
        }
        else
        {
            // if size remains the same, avoid expensive recreation of the entire view
            // just redraw.
            invalidateRect(nullptr);
        }
	}
#endif

	// invalidate something on the structure view itself (not the plugin area)
	void ModuleViewStruct::invalidateMyRect(gmpi::drawing::Rect localRect)
	{
		parent->ChildInvalidateRect(offsetRect(localRect, { bounds_.left, bounds_.top }));
	}

} // namespace.
