#include <algorithm>
#include "CLine2.h"
#include "Plug.h"
#include "CContainer.h"
#include "SynthEditDocBase.h"
#include "Application.h"
#include "UgDatabase.h"
#include "it_plug_destinations.h"
#include "PlugIO4.h"
#include "SerializationHelper_XML.h"
#include "Notify_msg.h"
#include "resource.h"

using namespace std;

namespace
{
bool res = ModuleFactory()->RegisterModule(new Module_Info(L"Line", IDS_MG_DEBUG, IDS_MG_DEBUG, 0, 0, CF_STRUCTURE_VIEW/*, 42*/) );
}


CLine2::CLine2() : // CDocOb( ModuleFactory()->GetById((L"Line")) )
	m_struct_rect{} // must be zeroed after serialise for paste to work ok
	,lineType_(DEFAULT_STYLE)
{
	// FromPlug/ToPlug are null-initialised in the header. That used to be done here
	// under _DEBUG only ("prevent spurious error during destruction on exception
	// during serialise"), which left release builds reading uninitialised pointers
	// out of isValid() whenever Import() couldn't resolve a module handle.
}

CLine2::CLine2(IPlug* from, IPlug* to) : CDocOb( ModuleFactory()->GetById(L"Line") ) 
	,lineType_(DEFAULT_STYLE)
{
	// ensure plugs in correct order
	bool flip = false;

	if( !from->isUnconnectedIOPlug() )
	{
		flip = from->GetDirection() == DR_IN;
	}
	else
	{
		if( !to->isUnconnectedIOPlug() )
		{
			flip = to->GetDirection() == DR_OUT;
		}
		else
		{
			// connecting two IO Mods. Tricky to determine direction.
			it_plug_destinations it( from );
			it.First();

			if( !it.IsDone() )
			{
				// from is connected to something tangible.
				flip = from->GetDirection() == DR_IN;
			}
			else
			{
				// Either 'to' is connected somewhere tangible, or we have no idea. Guess.
				flip = to->GetDirection() == DR_OUT;
			}
		}
	}

	if( flip )
	{
		ToPlug = from;
		FromPlug = to;
	}
	else
	{
		FromPlug = from;
		ToPlug = to;
	}

	OnNewConnection();
}

// inform plugs of new connection
void CLine2::OnNewConnection()
{
	FromPlug->OnNewConnection(this);
	ToPlug->OnNewConnection(this); // to plug last as connecting it may generate propagate backward msg
}

CLine2::~CLine2()
{
	if( Container() )
	{
		Container()->Remove(this);
//		NotifySafe(OM_DELETE);
	}
	else
	{
//		_RPT0(_CRT_WARN, "~CLine2() Unable to inform Container to remove me!!!\n");
	}

	// Diconnection in two steps:
	// 1 - Remove connector from BOTH ends
	// 2 - Inform CUG (for redraw etc)
	// very important that SE don't attempt to update screen with only one end disconnected (CRASH!)
	// worst case is wire between two plugs on same IO Mod (both auto-delete due to zero external connections)
	// Inform plugs what is happening
	if(FromPlug)
		FromPlug->Disconnect_pt1(this);

	if(ToPlug)
		ToPlug->Disconnect_pt1(this);

	if(FromPlug)
		FromPlug->Disconnect_pt2(this);

	if(ToPlug)
		ToPlug->Disconnect_pt2(this);
}

// this is called to prevent recursion. eg when plug is deleted it deletes line,
// when line is deleted it deletes io plug (indirectly)
void CLine2::OnPlugDelete(IPlug* p)
{
	if( FromPlug == p)
	{
		FromPlug = nullptr;
	}
	else
	{
		assert( ToPlug == p);
		ToPlug = nullptr;
	}
}

namespace
{
// "Osc1.Audio Out", for diagnostics.
std::wstring describePlug(IPlug* p)
{
	if (!p)
		return L"<nothing>";

	if (!p->UG())
		return L"<orphaned pin \"" + p->getName() + L"\">";

	return L"\"" + p->UG()->GetName() + L"." + p->getName() + L"\"";
}
}

bool CLine2::canSerialize(std::wstring& whyNot)
{
	whyNot.clear();

	if (!FromPlug || !ToPlug || !FromPlug->UG() || !ToPlug->UG())
	{
		whyNot = L"Connector " + describePlug(FromPlug) + L" to " + describePlug(ToPlug)
			+ L" was not saved: one end is not attached to a module.";
		return false;
	}

	// getPinNumber() answers "where is this plug in its own module's plug list?" and
	// returns -1 when it isn't there at all. Saving that wrote fPlg="-1"/tPlg="-1",
	// which loads back as a negative index and costs the user the connection.
	if (getPinNumber(FromPlug) < 0 || getPinNumber(ToPlug) < 0)
	{
		whyNot = L"Connector " + describePlug(FromPlug) + L" to " + describePlug(ToPlug)
			+ L" was not saved: a pin is missing from its own module's pin list.";
		return false;
	}

	return true;
}

void CLine2::Export(tinyxml2::XMLElement* moduleElement, ExportFormatType targetType)
{
	// Callers skip lines that fail canSerialize() (CContainer reports them), so
	// reaching here with one is a bug. Write nothing rather than a connector the
	// loader can't resolve - an empty element is dropped cleanly on import, a
	// negative pin index is not.
	{
		std::wstring whyNot;
		if (!canSerialize(whyNot))
		{
			assert(false);
			return;
		}
	}

	CDocOb::Export(moduleElement, targetType);

	XmlSaveHelper helper(moduleElement);
	Serialise2(helper);

	moduleElement->SetAttribute("fMod", FromPlug->UG()->Handle());
	moduleElement->SetAttribute("tMod", ToPlug->UG()->Handle());

	// Pin 0 is the load-time default, so it's left out to keep files small.
	auto p = getPinNumber(FromPlug);
	if( p != 0)
		moduleElement->SetAttribute("fPlg", p);

	p = getPinNumber(ToPlug);
	if (p != 0)
		moduleElement->SetAttribute("tPlg", p);

	if (!nodes.empty())
	{
		auto nodesE = moduleElement->GetDocument()->NewElement("nodes");
		moduleElement->LinkEndChild(nodesE);

		for (auto& n : nodes)
		{
			auto nodeX = moduleElement->GetDocument()->NewElement("node");
			nodeX->SetAttribute("x", (int) n.x);
			nodeX->SetAttribute("y", (int) n.y);

			nodesE->LinkEndChild(nodeX);
		}
	}
}

void CLine2::Import(std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement* moduleElement, ExportFormatType targetType)
{
	CDocOb::Import(uniqueIds, moduleElement, targetType);

	XmlLoadHelper helper(moduleElement);
	Serialise2(helper);

	{
		int tModule = 0;
		int fModule = 0;
		int tPlug = 0;
		int fPlug = 0;
		moduleElement->QueryIntAttribute("tMod", &tModule);
		moduleElement->QueryIntAttribute("fMod", &fModule);
		moduleElement->QueryIntAttribute("tPlg", &tPlug);
		moduleElement->QueryIntAttribute("fPlg", &fPlug);

		auto itf = uniqueIds.find(fModule);
		auto itt = uniqueIds.find(tModule);
		//auto fUg = dynamic_cast<CUG*>(Document()->uniqueIdDatabase.HandleToObjectWithNull(fModule));
		//auto tUg = dynamic_cast<CUG*>(Document()->uniqueIdDatabase.HandleToObjectWithNull(tModule));

		if (itf != uniqueIds.end() && itt != uniqueIds.end())
		{
			FromPlug = (*itf).second->GetPlugForSerialize(fPlug);
			ToPlug = (*itt).second->GetPlugForSerialize(tPlug);

			if (FromPlug && ToPlug)
			{
				FromPlug->AddConnectorQuiet(this);
				ToPlug->AddConnectorQuiet(this);
			}
		}
	}

	auto plugsElement = moduleElement->FirstChildElement("nodes");
	std::vector< std::pair<int, std::string> > plugDefaults;

	if (plugsElement)
	{
		for (auto plugElement = plugsElement->FirstChildElement(); plugElement; plugElement = plugElement->NextSiblingElement())
		{
			assert(strcmp(plugElement->Value(), "node") == 0);
			int x{}, y{};
			plugElement->QueryIntAttribute("x", &x);
			plugElement->QueryIntAttribute("y", &y);

			nodes.push_back({ x, y });
		}
	}
}

// when loading, module may not have the same pins anymore.
bool CLine2::isValid()
{
	return FromPlug && ToPlug;
}

float errorToWidth(float errorMax)
{
	if (errorMax > -80.f)
	{
		return (errorMax + 100.f) / 100.f;
	}
	else
	{
		return 0.2f * (errorMax + 200.f) / 120.f;
	}
}

void CLine2::Export(Json::Value& object_json, ExportFormatType targetType)
{
	// No Lines need to be drawn on GUI.
	if (targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL || targetType == SAT_CADMIUM_VIEW)
	{
		return;
	}

	if (targetType == SAT_SYNTHEDIT_GUI_STRUCT)
	{
#if defined( _DEBUG )
		auto& sortedResults = Document()->cancellationResults;

		if (!sortedResults.empty())
		{
			if (!FromPlug->isIoPlug())
			{
				int plug_num = 0;
				for (auto p : FromPlug->UG()->Plugs)
				{
					const bool isDspPinOut = p->GetDirection() == DR_OUT && p->getDatatype() == DT_FSAMPLE && !p->isUiPlug();
					if (p == FromPlug)
					{
						break;
					}

					if (isDspPinOut)
						plug_num++;
				}


				{
					float maxError = -1000.0f;
					for (const auto& r : sortedResults)
					{
						if (r.handle == FromPlug->UG()->Handle() && plug_num == r.pin) // && (r.voice == -1 || r.voice == 1)) // Element plays voice 1
						{
							maxError = (std::max)(maxError, r.errorMax);
						}
					}

					if (maxError > -1000.f)
						object_json["Cancellation"] = errorToWidth(maxError);
				}
			}
			else
			{
				auto otherPin = dynamic_cast<CPlugIO4*>(FromPlug)->GetTiedTo();
				if (otherPin)
				{
					// trace fromplug back thru containers if nesc
					it_plug_destinations it(otherPin);
					for (it.First(); !it.IsDone(); it.Next())
					{
						IPlug* fromP = it.CurrentItem();

						for (const auto& r : sortedResults)
						{
							if (r.handle == fromP->UG()->Handle() && (r.voice == -1 || r.voice == 1)) // Element plays voice 1
							{
								int plug_num = 0;
								for (auto p : fromP->UG()->Plugs)
								{
									// Based on active cords count, ie not going to muted objects
									const bool isDspPinOut = p->GetDirection() == DR_OUT && p->getDatatype() == DT_FSAMPLE && !p->isUiPlug();
									if (p == fromP)
									{
										if (isDspPinOut && plug_num == r.pin)
										{
											float cancellationVisualize = {}; // 0db bad -100 not-bad.
											if (r.errorMax > -80.f)
											{
												cancellationVisualize = (r.errorMax + 100.f) / 100.f;
											}
											else
											{
												cancellationVisualize = 0.2f * (r.errorMax + 200.f) / 120.f;
											}

											object_json["Cancellation"] = cancellationVisualize;
										}
										break;
									}

									if (isDspPinOut)
										plug_num++;
								}
							}
						}
					}
				}
			}
		}
#endif

	}

	object_json["fMod"] = FromPlug->UG()->Handle();
	object_json["tMod"] = ToPlug->UG()->Handle();

#if 0 // Outputting pins *index*, don't work for sub-controls. See PatchMemoryIntOutGui which has non-consecutive ids.
	int fromIdentifier = FromPlug->getRuntimePinIndex(FromP, targetType != SAT_SUBCONTROLS_GUI);
	int toIdentifier = ToPlug->getRuntimePinIndex(ToP, targetType != SAT_SUBCONTROLS_GUI);
#else
	int fromIdentifier = FromPlug->UG()->getPinNumber(FromPlug);
	int toIdentifier = ToPlug->UG()->getPinNumber(ToPlug);
#endif

	object_json["fPlg"] = fromIdentifier;
	object_json["tPlg"] = toIdentifier;
	// Resolve the per-line style down to the binary the (passive) view understands:
	// 0 = curvey, anything else = straight. A line set to DEFAULT_STYLE follows the
	// application-wide "Default Line Style" preference.
	int effectiveType = lineType_;
	if (effectiveType == DEFAULT_STYLE)
		effectiveType = (Application() && Application()->defaultLinesCurved()) ? CURVEY : STRAIGHT;

	if (effectiveType == CURVEY)
		object_json["lineType"] = CURVEY; // omit otherwise → view defaults to straight

	if (!nodes.empty())
	{
		Json::Value nodes_json(Json::arrayValue);
		for (auto& n : nodes)
		{
			Json::Value node_json(Json::objectValue);
			node_json["x"] = (int) n.x;
			node_json["y"] = (int) n.y;
			nodes_json.append(node_json);
		}
		object_json["nodes"] = nodes_json;
	}

	if (targetType == SAT_SYNTHEDIT_GUI_STRUCT)
	{
		object_json["highlightFlags"] = getErrorFlags();
	}

	return CDocOb::Export(object_json, targetType);
}

bool CLine2::IsCopyTagged()
{
	if( Container() && Container()->IsCopyTagged() )
	{
		return true; // container_being_copied
	}

	// changed to allow load prefab to work (lines wern't loading)
	return ( FromPlug->UG()->IsCopyTagged() && ToPlug->UG()->IsCopyTagged() );
}

gmpi::drawing::RectL CLine2::getViewObRect(int p_view_type)
{
	if( p_view_type == CF_STRUCTURE_VIEW )
		return m_struct_rect;
	else
		return CDocOb::getViewObRect(p_view_type);
}

void CLine2::setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect)
{
	if( m_struct_rect != p_rect )
	{
		m_struct_rect = p_rect;
	}
}

void CLine2::offsetViewObRect( int p_view_type, int dx, int dy )
{
	if (p_view_type == CF_STRUCTURE_VIEW)
	{
		for( auto& n : nodes )
		{
			n.x += dx;
			n.y += dy;
		}
	}
}

void CLine2::Drag(int p_view_type, int dx, int dy)
{
	const int dragNodesCloserThan = 50;

	gmpi::drawing::RectL dragwithin;
	if( FromPlug->UG()->GetSelected() )
	{
		if( ToPlug->UG()->GetSelected() ) // both ugs. Drag entire line.
		{
			dragwithin.bottom = dragwithin.right = INT_MAX / 2; // INT_MAX is too big, it overflows subsequent calculations.
			dragwithin.top = dragwithin.left = -INT_MAX / 2;
		}
		else
		{
			dragwithin = FromPlug->UG()->getViewObRect(p_view_type);
		}
	}
	else
	{
		if( !ToPlug->UG()->GetSelected() ) // neither ug's elected
		{
			return;
		}
		dragwithin = ToPlug->UG()->getViewObRect(p_view_type);
	}

	// Non-resizeable modules don't report right or bottom. make some assumptions
	if (gmpi::drawing::getWidth(dragwithin) < 5)
	{
		dragwithin.right = dragwithin.left + 60; // assumed module width
	}
	if (gmpi::drawing::getHeight(dragwithin) < 5)
	{
		dragwithin.bottom = dragwithin.top + 60; // assumed module height
	}

	dragwithin.left -= dragNodesCloserThan;
	dragwithin.top -= dragNodesCloserThan;
	dragwithin.right += dragNodesCloserThan;
	dragwithin.bottom += dragNodesCloserThan;

	bool nodesMoved = false;
	for( auto it = nodes.begin() ; it != nodes.end() ; ++it )
	{
		int x = (*it).x;
		int y = (*it).y;

		if( dragwithin.top < y && dragwithin.bottom > y && dragwithin.left < x && dragwithin.right > x)
		{
			(*it).x += dx;
			(*it).y += dy;
			nodesMoved = true;
		}
	}

	if (nodesMoved)
	{
		Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_STRUCT, (void*)this);
	}
}

bool CLine2::HighlightLineTo(CUG* cug, int32_t flags)
{
	bool ret = false;

	if( getErrorFlag() || ToPlug->UG() == cug )
	{
		Highlight(flags);
		ret = true;
	}
	else
	{
		if( ToPlug->isIoPlug() )
		{
			CPlugIO4* tied_to_plug = dynamic_cast<CPlugIO4*>(ToPlug)->GetTiedTo();
			if( tied_to_plug )
			{
				for(auto it = tied_to_plug->Connectors().begin() ; it != tied_to_plug->Connectors().end() ; ++it )
				{
					CLine2* line = *it;

					if( line->HighlightLineTo(cug, flags) )
					{
						Highlight(flags);
						ret = true;
					}
				}
			}
		}
	}

	return ret;
}

void CLine2::InsertNode(int i, double x, double y)
{
	CLine2::Point p;
	p.x = (int) x;
	p.y = (int) y;

//	_RPT1(_CRT_WARN, "InsertNode %d\n", i);

	// Don't bother storing first or last node (determined by plugs).
	if( i > nodes.size() )
		nodes.push_back(p);
	else
		nodes.insert(nodes.begin( ) + i - 1,p);
/*
//	_RPT0(_CRT_WARN, "----nodes-----\n");
	for (auto n : nodes)
	{
//		_RPT2(_CRT_WARN, "(%d,%d)\n",n.x, n.y);
	}
*/

	SetModifiedFlag();
}

void CLine2::DeleteNode(int i)
{
	nodes.erase(nodes.begin( ) + i - 1);

	SetModifiedFlag();
}

void CLine2::UpdateNode(int i, double x, double y)
{
	CLine2::Point p;
	p.x = (int) x;
	p.y = (int) y;
	// Don't bother storing first or last node (determined by plugs).
	nodes[i - 1] = p;

	SetModifiedFlag();
}

void CLine2::Straighten()
{
	nodes.clear();

	SetModifiedFlag();
}

