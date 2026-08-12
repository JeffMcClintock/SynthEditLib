
#include <algorithm>
#include "Plug4.h"
#include "DocOb.h"
#include "CLine2.h"
#include "SuspendDSP.h"
#include "Application.h"
#include "CUG.h"
#include "CContainer.h"
#include "SynthEditDocBase.h"
#include "PatchParameter.h"
#include "UgDatabase.h"
#include "PlugIO4.h"
#include "tinyxml/tinyxml.h"
#include "../tinyXml2/tinyxml2.h"
#include "Notify_msg.h"
#include "./Plug_decorator_sdk2.h"
#include "it_plug_destinations.h"

bool areEquivalentDefaults(const std::wstring& a, const std::wstring& b, EPlugDataType dtype)
{
	if (a == b)
		return true;

	// consider numeric equivalence.
	if (dtype == DT_INT || dtype == DT_INT64 || dtype == DT_ENUM)
	{
		return StringToInt(a) == StringToInt(b);
	}
	else if (dtype == DT_FLOAT || dtype == DT_DOUBLE || dtype == DT_FSAMPLE)
	{
		return StringToDouble(a) == StringToDouble(b);
	}
	return false;
}

CPlug4::CPlug4( CUG* p_ug, InterfaceObject* i ) : PlugDescriptionDecorator(dynamic_cast<IPlugDescriptionDecorator*>(i)), IPlug(i)
	, cug_(p_ug)
{
	m_default = i->GetDefaultVal();
}

CPlug4::~CPlug4()
{
	// _RPT1(_CRT_WARN, "~CPlug4() %x\n", this );
	RemoveLines();
	NotifySafe2(OM_DELETE, 0);
	getPlugDescription()->DeleteDecorators(this);
}

void CPlug4::Export(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	getPlugDescription()->Export(self, xml, targetType);

	// get InterfaceObject
	auto ultimate = getPlugDescription();
	while (ultimate->getPlugDescription())
		ultimate = ultimate->getPlugDescription();
	auto desc = dynamic_cast<InterfaceObject*>(ultimate);

	if(can_set_value() && !areEquivalentDefaults((std::wstring) m_default, desc->GetDefaultVal(), getDatatype()))
		xml->SetAttribute("default", WStringToUtf8((std::wstring) m_default).c_str());
}

void CPlug4::Import(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	{
		const char* s = "";
		if (tinyxml2::XML_SUCCESS == xml->QueryStringAttribute("default", &s))
		{
			SetDefaultQuiet(Utf8ToWstring(s));
		}
	}

	{
		const char* n = "";
		if (tinyxml2::XML_SUCCESS == xml->QueryStringAttribute("name", &n))
		{
			// will create a new decorator
			getPlugDescription()->setName(self, Utf8ToWstring(n));
		}
	}

	{
		int parameterId = -1;
		if (tinyxml2::XML_SUCCESS == xml->QueryIntAttribute("parameterId", &parameterId))
		{
			auto decorator = new Plug_decorator_sdk2();
			decorator->setParameterId(parameterId);
			AddDecorator(decorator);
		}
	}

	getPlugDescription()->Import(self, xml, targetType);
}


// !!! can be non-member if m_connectors abstracted to iterator
bool CPlug4::IsConnectedTo(IPlug* p_to)
{
	for( auto it = m_connectors.begin() ; it != m_connectors.end() ; ++it )
	{
		CLine2* l = *it;

		if( l->FromPlug == p_to || l->ToPlug == p_to )
			return true;
	}

	return false;
}

CContainer* CPlug4::Container()
{
	return UG()->Container();
}

// How many lines connected to this (not counting muted mods)?
// Need to get info from parent container as it holds the lines
bool CPlug4::HasActiveConnections()
{
	for (auto line : m_connectors)
	{
		IPlug* other{};

		if (line->FromPlug == this)
		{
			other = line->ToPlug;
		}
		else
		{
			assert(line->ToPlug == this);
			other = line->FromPlug;
		}

		if (isUiPlug() || !other->UG()->GetMute())
		{
			return true;
		}
	}

	return false;
}

void CPlug4::OnNewConnection(CLine2* p_line)
{
	bool wasUnusedSpare = isUnusedSpare();
	assert( std::find( m_connectors.begin(), m_connectors.end(), p_line) == m_connectors.end() );
	m_connectors.push_back( p_line );
	UG()->OnNewConnection( p_line );

	if( isSettableOutput() && GetNumConnections() == 1 )
	{
		SetDefault(p_line->ToPlug->GetDefault() );
	}

	if( wasUnusedSpare )
	{
		if( isRenamable() )
		{
			IPlug* dest = p_line->ToPlug;

			if( dest == this )
			{
				dest = p_line->FromPlug;
			}

			setName( dest->getName() );
		}

		auto p = Duplicate();
		UG()->AddPlug( p );
		p->Initialise();
	}

	// propagate list backward
	// new: GUI plugs now send list forward (plugs reversed SE 1.1)
	if( getDatatype() == DT_ENUM)
	{
		if( !isUiPlug() )
		{
			PropogateBack( this, OM_DOWNSTREAM_PLUG_ENUM_CHANGE );
		}

		// since GUI plug directions reversed in SE1.1
		if( isOldStyleGuiPlug() )
		{
			PropogateForward( this, OM_DOWNSTREAM_PLUG_ENUM_CHANGE );
		}
	}

	// support for setting up sliders name, default etc on first real connection (not counting unconnected IO mods)
	if( p_line->ToPlug == this )
	{
		// don't propogate back to modules already connected 
		// propogate only down fresh connction.(else sliders already connected mysteriously reset).
		p_line->FromPlug->PropogateBack( this, OM_DOWNSTREAM_PLUG_CONNECT2 );

		// Allow GUI plugs to connect.
		if( isUiPlug() )
		{
			PropogateForward( this, OM_UPSTREAM_PLUG_CONNECT );
		}
	}
}

void CPlug4::OnUiDisconnect()
{
	if( GetDirection() == DR_CNTRL ) // control has only one possible connection. easy to disconnect
	{
		Container()->VO_Notify( OM_DISCONNECT_IN_GUI_CONNECTIONS);
	}
}

void CPlug4::Disconnect_pt1( CLine2* p_line )
{
	// Allow GUI plugs to disconnect. p_line can be nullptr during cut/paste.
	if( p_line && p_line->ToPlug == this && isUiPlug() )
	{
		PropogateForward( this, OM_UPSTREAM_PLUG_DISCONNECT );
	}

	m_connectors.remove( p_line );
}

void CPlug4::Disconnect_pt2(CLine2* p_line)
{
	UG()->OnDisconnect( this );

	if( canRemove() )
	{
		CUG* ug = UG(); // store localy cause this object about to be deleted.
		ug->RemovePlug(this);

		if( ug->Container() )
		{
			ug->Container()->VO_Notify( OM_LAYOUT_CHANGE2, ug );
		}
	}
}

CPlug4* CPlug4::Duplicate()
{
	// find ultimate plugdescription
	IPlugDescriptionDecorator* i = getPlugDescription();

	while( i->getPlugDescription() )
	{
		i = i->getPlugDescription();
	}

	auto p = UG()->MakePlug(dynamic_cast<InterfaceObject*>(i) );

	if( i->autoDuplicate(0) )
	{
		if( UG()->getType()->ModuleTechnology() >= MT_SDK3 )
		{
			int newId = UG()->Plugs.back()->getPlugDescID() + 1;
			p->setPlugDescID( newId );
			// TODO: re-make in-GUI connections. 2 - handle multiple autoduplicate plugs
		}
		else
		{
			// Fix for DH TextAppend (autoduplicating parameter plugs)
			if( isParameterPlug() && i->GetDirection(0) == DR_OUT && isOldStyleGuiPlug() )
			{
				int parameterId = i->getParameterId(0);
				parameter_description* pd = UG()->getType()->getParameterById( parameterId );

				if( pd )
				{
					parameter_description newParamDescrip = *pd;
					// choose next available parameter ID.
					parameterId = -1;

					for( auto it = UG()->Plugs.begin() ; it != UG()->Plugs.end() ; ++it )
					{
						IPlug* iplg = *it;

						if( iplg->getParameterId() > parameterId )
						{
							parameterId = iplg->getParameterId();
						}
					}

					++parameterId;
					newParamDescrip.id = parameterId;
					// hacky. Assign next parameter ID to this plug.
					IPlugDescriptionDecorator* pd3 = dynamic_cast<IPlugDescriptionDecorator*>(this)->getPlugDescription();

					while( pd3 )
					{
						Plug_decorator_sdk2* pd2 = dynamic_cast<Plug_decorator_sdk2*>(pd3);

						if( pd2)
						{
							pd2->setParameterId( newParamDescrip.id );
							break;
						}

						pd3 = pd3->getPlugDescription();
					}

					UG()->get_patch_manager()->RegisterParameter( UG(), newParamDescrip );
				}
			}
		}
	}

	return p;
}

// see also: it_plug_destinations.
IPlug* CPlug4::GetUltimateDest2( EDirection dr )
{
	// hacky?, used to shortcut proper methods. should be private to CPlug2. !!!!
	if( GetDirection() == dr )
		return this;

	if( ConnectedTo() )
	{
		assert( GetDirection() != ConnectedTo()->GetDirection() );
		return ConnectedTo()->GetUltimateDest2( dr );
	}

	return 0;
}

// IPlug non-member function
bool canConnectplugs(IPlug* p_from, IPlug* p_to, std::wstring& p_error_msg )
{
	p_error_msg = (L"");

	// No point dragging something to itself
	if( p_to == p_from )
		return false;

	// ensure plugs not already connected
	if( p_from->UG() == p_to->UG() )
	{
		p_error_msg = (L"Can't connect module to itself.");
		return false;
	}

	EPlugDataType from_datatype = p_from->getDatatype();
	EPlugDataType to_datatype = p_to->getDatatype();

	// spare container plugs handle any connection.
	bool fromIsUnusedContainerPlug;
	bool toIsUnusedContainerPlug;

	if( p_from->isIoPlug() )
	{
		CPlugIO4* tiedTo = dynamic_cast<CPlugIO4*>(p_from)->GetTiedTo();
		fromIsUnusedContainerPlug = p_from->GetNumConnections() == 0 && (tiedTo == 0 || tiedTo->GetNumConnections() == 0);
	}
	else
	{
		fromIsUnusedContainerPlug = false;
	}

	if( p_to->isIoPlug() )
	{
		CPlugIO4* tiedTo = dynamic_cast<CPlugIO4*>(p_to)->GetTiedTo();
		toIsUnusedContainerPlug = p_to->GetNumConnections() == 0 && (tiedTo == 0 || tiedTo->GetNumConnections() == 0);
	}
	else
	{
		toIsUnusedContainerPlug = false;
	}

	if( !fromIsUnusedContainerPlug && !toIsUnusedContainerPlug )
	{
		if( p_from->GetDirection() == p_to->GetDirection() )
		{
			p_error_msg = (L"Can't connect. An output must go to an input (plugs on the left = Inputs, right = Outputs");
			//		p_error_msg = (L"Sorry, you cannot connect two inputs or outputs together. An output plug must always go to an input. Plugs on the Left of a module are Inputs. Plugs on the Right of a module are Outputs.");
			return false;
		}

		if( p_from->isUiPlug() != p_to->isUiPlug() )
		{
			p_error_msg = (L"Can't connect different plug types (GUI->DSP)");
			return false;
		}

		if (p_from->isUiPlug())
		{
			// both same datatype.
			bool result{};

			// struct/object pins carry a subtype (e.g. "struct:color"); a raw BLOB is the
			// untyped wildcard into a struct. (blob<->object is DSP-only - it needs the BlobToBlob2 converter.)
			if (from_datatype == to_datatype)
			{
				// struct/object: subtypes must match (target subtype "any" is a wildcard). Others: OK.
				result = (from_datatype != DT_STRUCT && from_datatype != DT_OBJECT)
					|| p_from->getClassName() == p_to->getClassName()
					|| p_to->getClassName() == "any";
			}

			// A raw BLOB connects to any struct (either direction).
			if (!result && (from_datatype == DT_BLOB || from_datatype == DT_STRUCT))
			{
				result = (from_datatype == DT_BLOB && to_datatype == DT_STRUCT)
					|| (from_datatype == DT_STRUCT && to_datatype == DT_BLOB);
			}

			if (!result && to_datatype == DT_STRUCT && p_to->getClassName() == "any")
			{
				result = from_datatype == DT_FLOAT;
			}

			// GUI pins of different datatypes can connect if a GUI converter module exists
			// (auto-inserted by ViewBase::ConnectModules at load time).
			if (!result && getGuiConverterId(from_datatype, to_datatype) != nullptr)
			{
				result = true;
			}

			if (!result)
			{
				p_error_msg = (L"Can't connect these plug types directly (e.g. you can't connect a voltage pin to a filename). Consider using a converter module.");
				return false;
			}
		}
		else
		{
			// DSP plugin are often convertible.
			if (!AreCompatible(from_datatype, to_datatype))
			{
				p_error_msg = (L"Can't connect these plug types directly (e.g. you can't connect a voltage pin to a filename). Consider using a converter module.");
				return false;
			}

			// struct/object pins carry a subtype (e.g. "struct:color", "object:MyType"). Two pins of
			// the same typed family may connect only if their subtypes match (target subtype "any" is a
			// wildcard). A raw BLOB is exempt - it's the untyped wildcard into struct, and auto-converts to object.
			if (from_datatype == to_datatype && (from_datatype == DT_STRUCT || from_datatype == DT_OBJECT)
				&& p_from->getClassName() != p_to->getClassName()
				&& p_to->getClassName() != "any")
			{
				p_error_msg = (L"Can't connect these pins - their subtypes are different.");
				return false;
			}
		}
	}

	if( !p_from->canAcceptConnection())
	{
		p_error_msg = (L"This type of plug accepts only one output");
		return false;
	}

	if( !p_to->canAcceptConnection())
	{
		p_error_msg = (L"This type of plug accepts only one input");
		return false;
	}

	// Enum list types must match
	if( p_to->getDatatype() == DT_ENUM )
	{
		IPlug* masterPlug = 0;
		IPlug* slavePlug = 0;

		// output plugs (slave) get enum list from what they connect to (master).
		if( p_from->GetDirection() == DR_OUT )
		{
			slavePlug = p_from;
			masterPlug = p_to;
		}
		else
		{
			slavePlug = p_to;
			masterPlug = p_from;
		}

		// GUI are opposite
		if( p_from->isUiPlug() )
		{
			IPlug* temp = slavePlug;
			slavePlug = masterPlug;
			masterPlug = temp;
		}

		// don't check on first connection because output plug is going to pick up new plugs enum list anyhow.
		//		if( slavePlug->GetNumConnections() > 0 && slavePlug->CheckEnumOnConnection())
		if( slavePlug->GetNumConnections() > 0 )//&& masterPlug->CheckEnumOnConnection())
		{
			// compare new plug's enum list with enum_entry's existing connection's
			if(	!enums_are_compatible( p_from->getDefaultEnumList() , p_to->getDefaultEnumList()) )
			{
				p_error_msg = (L"These plug's lists are not compatible");
				return false;
			}
		}
	}

	// ensure plugs not already connected
	if( p_from->IsConnectedTo( p_to ) )
	{
		p_error_msg = (L"These two plugs are already connected");
		return false;
	}

	return true;
}

// IPlug non-member function
bool PlugSortOrderOK( IPlug* p_first, IPlug* p_second )
{
	// Pins are sorted by:
	// - normal vs IO Plug
	// - GUI (first), then DSP.
	// - Each group in ID order.
	// EXCEPT old-style Listinterface (SDK2 and some internal modules) which are purely in ID order to retain old idx to pin mapping. for e.g. seGuiHostPlugGetVal
	if( p_first->isIoPlug() != p_second->isIoPlug() )
	{
		return p_first->isIoPlug() < p_second->isIoPlug();
	}

	if( p_first->isUiPlug() == p_second->isUiPlug() || (p_first->UG()->getType()->GetFlags() & CF_OLD_STYLE_LISTINTERFACE) != 0 )
	{
		//	duplicating plugs have same ID...	assert( p_first->getPlugDescID() != p_second->getPlugDescID() );
		return p_first->getPlugDescID() < p_second->getPlugDescID();
	}

	return p_first->isUiPlug() > p_second->isUiPlug();
}

// IPlug non-member function
CDocOb* ConnectPlugs( IPlug* p_from, IPlug* p_to, int32_t handle )
{
	// callers responsibility to check this.
#ifdef _DEBUG
	std::wstring error_msg;
	assert(canConnectplugs(p_from, p_to, error_msg));
#endif

	p_from->UG()->SetModifiedFlag();
	SuspendDSP s( p_from->UG()->Document()->Application() );
	return p_from->UG()->Container()->AddLine( p_from, p_to, handle);
}

// IPlug non-member function
void MoveLinesFrom( IPlug* old_plug, IPlug* new_plug )
{
	for( auto it = old_plug->Connectors().begin() ; it !=old_plug->Connectors().end() ; ++it )
	{
		CLine2* l = *it;

		if( l->FromPlug == old_plug )
		{
			l->FromPlug = new_plug;
		}
		else
		{
			assert( l->ToPlug == old_plug );
			l->ToPlug = new_plug;
		}

		new_plug->Connectors().push_back(l);
	}

	old_plug->Connectors().clear();
}

// What, if anything is this connected to
IPlug* CPlug4::ConnectedTo()
{
	if (Connectors().empty())
		return nullptr;
	
	auto line = Connectors().front();
	if( line->FromPlug == this )
	{
		return line->ToPlug;
	}
	else
	{
		assert( line->ToPlug == this );
		return line->FromPlug;
	}
}

std::wstring CPlug4::getDefaultEnumList()
{
	if( (GetDirection() == DR_OUT) == isOldStyleGuiPlug() ) // fix for SDK2 (ui_enum_to_bools) (since 1.1 enum pin is now OUTPUT)
	{
		std::wstring enumList = getPlugDescription()->getDefaultEnumList(this);

		// enumList also used for file extensions on text pins. In which case don't call CustomiseEnumList(), it will wipe out file extension.
		if( getPlugDescription()->getDatatype(this) == DT_ENUM )
		{
			// give ug a chance to customise the default enum list
			return UG()->CustomiseEnumList( this, enumList );
		}
		else
		{
			return enumList;
		}
	}
	else
	{
		it_plug_destinations it(this);
		it.First();
		if (!it.IsDone())
			return it.CurrentItem()->getDefaultEnumList();
	}

	return {};
}

// propgate a message backward from a plug
// usefull way to let a control know that what it is ultimately connected to has changed
// message is just an integer
void CPlug4::PropogateBack(IPlug* plug, int msg_id)
{
	// Avoid resetting controls to defaults when containerizing
	if (CUG::is_containerizing)
		return;

	//difficult	assert( UG()->Document()->isGraphInitialised() );
	if( GetDirection() == DR_IN )
	{
		for( auto it = Connectors().begin() ; it != Connectors().end() ; ++it )
		{
			CLine2* line = *it;

			if( line->ToPlug == this && line->FromPlug )  // Fromplug may be nullptr due to OnPlugDelete() called when entire module is deleted (not just line)
			{
				line->FromPlug->PropogateBack( plug, msg_id );
			}
		}
	}
	else
	{
		UG()->OnDownstreamPlugChange( this, plug, msg_id );
	}
}

// since SE 1.1 GUI plug directions reversed, legacy modules need this..
void CPlug4::PropogateForward(IPlug* plug, int msg_id)
{
	if( GetDirection() == DR_OUT )
	{
		for( auto it = Connectors().begin() ; it != Connectors().end() ; ++it )
		{
			CLine2* line = *it;

			if( line->FromPlug == this ) //&& line->ToPlug ) // Fromplug may be nullptr due to OnPlugDelete() called when entire module is deleted (not just line)
			{
				line->ToPlug->PropogateForward( plug, msg_id );
			}
		}
	}
	else
	{
		UG()->OnDownstreamPlugChange( this, plug, msg_id );
	}
}
void CPlug4::RemoveLines()
{
	while( !m_connectors.empty() )
	{
		CLine2* l = m_connectors.front();
		l->OnPlugDelete( this ); // prevent line trying to delete plug indirectly
		m_connectors.pop_front();
		l->OnDelete();
	}
}

// on loading, module type information is a cached copy from the project file.
// update my pointer to the 'live' module data from local filesystem.
void CPlug4::AdjustDecoratorPointer()
{
	// switch from file plug desc to factory plug desc
	// traverse decorators to last one
	IPlugDescriptionDecorator* next_to_last = this;
	IPlugDescriptionDecorator* pd = next_to_last->getPlugDescription();

	while( pd->getPlugDescription() )
	{
		next_to_last = pd;
		pd = pd->getPlugDescription();
	}

	InterfaceObject* plugDescription{};

	if( isUiPlug() && !isOldStyleGuiPlug())
	{
		// SDK3 autoduplicate have artifical ID held by decorator, however final plug-description holds 'real' ID.
		plugDescription = UG()->getType()->getGuiPinDescriptionById( pd->getPlugDescID(this) );

		// IO plugs may appear to be GUI plugs, but they're only pretending.
		if( plugDescription == 0 )
		{
			assert( dynamic_cast<CPlugIO4*>(this) );
			plugDescription = (UG()->getType()->getPinDescriptionById( getPlugDescID() ));
		}
	}
	else
	{
		plugDescription = (UG()->getType()->getPinDescriptionById( pd->getPlugDescID(this) ));
	}

	if (!plugDescription && isOldStyleGuiPlug())
	{
		// sliders moved last plugs to gui_plugs. i.e. there are less plugs, but more gui plugs. Adjust.
		const auto numDspPins = UG()->getType()->PlugCount();
		const auto id = pd->getPlugDescID(this);

		if (id >= numDspPins)
		{
			plugDescription = (UG()->getType()->getGuiPinDescriptionByPosition(id - numDspPins));
		}
	}

	assert(plugDescription);

	if( plugDescription )
	{
		next_to_last->setPlugDescription(dynamic_cast<IPlugDescriptionDecorator*>(plugDescription) );
		info_ = plugDescription;
	}
//	else
//	{
////		assert( dynamic_cast<PlugDescriptionSelfContained*>( next_to_last->getPlugDescription() ) );
//	}
}

void CPlug4::Initialise(bool loaded_from_file )
{
	// init decorators (if any)
	getPlugDescription()->Initialise(this, loaded_from_file);

	if( loaded_from_file )
	{
		/// already in plug decorator sdk2... Make UiConnections();
		// earlier SE didn't serialise enum list or file extnesions, was re-asigned every load.
		// this is not strickly needed except when loading 1.0 files, or perhaps unless
		// connected pin changes enum-list.
		if( autoConfigureParameter() )
		{
			IPlug* ultimate = GetUltimateDest2();

			if( ultimate != 0 )
			{
				PatchParameter_base* patch_param = UG()->getFirstPatchParam();
				assert( patch_param );

				switch( getDatatype() )
				{
				case DT_STRING_UTF8:
				case DT_TEXT:
				{
					// File extension.
					std::wstring fileExtension = ( ultimate->getFileExt() );
					patch_param->SetValue(RawView(fileExtension), FT_FILE_EXTENSION);
				}
				break;

				case DT_ENUM:
				{
					// Enum List
					std::wstring enumList = ( ultimate->getDefaultEnumList() );
					patch_param->SetValue(RawView(enumList), FT_ENUM_LIST);
				}
				break;
                        
                default:
                    break;
				};
			}
		}

	}

	// create any needed host params. see also CUG::Sdk2UpdatePatchConnections()
	if( isHostControlledPlug() )
	{
		HostControls hostConnect = getHostConnect();

		if( hostConnect == HC_NONE )
			hostConnect = StringToHostControl( getName() );

		//if(hostConnect != HC_VOICE_VIRTUAL_VOICE_ID) // Currently supported DSP-side only. Could be supported.
		if(!AttachesToVoiceContainer(hostConnect)) // Performance host-controls now managed independant of patch-manager.
			UG()->get_patch_manager()->GetHostGeneratedParameter( hostConnect, true, Container(), getName().c_str() );
	}

	// (m_default has no change-observer: SetDefault runs the OM_GUI_PLUG_DEFAULT_CHANGE /
	// OnPlugDefaultChange / SetModified side-effects itself, and SetDefaultQuiet must stay quiet.)

#if defined( _DEBUG )
	PatchParameter_base* par = UG()->get_patch_manager()->GetParameter( UG(), getParameterId() );

	if( par != 0 )
	{
		// identify obscure bug where pasted knob attaches to wrong parameter.
		EPlugDataType plugDt = getDatatype();
		int rv;
		par->GetDatatype( (ParameterFieldType) getParameterFieldId(), &rv );
		EPlugDataType paramDt = (EPlugDataType) rv;

		if( plugDt != paramDt && !(plugDt == DT_ENUM && paramDt == DT_INT) && !(plugDt == DT_STRING_UTF8 && paramDt == DT_TEXT))
		{
			std::wstring name = DebugIdentify();
			assert( false && "pin type don't match parameter type" );
		}
	}

#endif
}

#if defined( _DEBUG )
std::wstring CPlug4::DebugIdentify()
{
	std::wstring name( ( UG()->GetName() ));
	name.append( L"/" );
	name.append( getName() );
	return name;
}
#endif

IPlugDescriptionDecorator* CPlug4::AddDecorator( IPlugDescriptionDecorator* p_decorator )
{
	if( getPlugDescription() == 0 || getPlugDescription()->getDecoratorSortOrder() > p_decorator->getDecoratorSortOrder() )
	{
		p_decorator->setPlugDescription( getPlugDescription() );
		setPlugDescription( p_decorator );
	}
	else
	{
		// decorators must always be in correct order;
		IPlugDescriptionDecorator* prev = getPlugDescription();
		IPlugDescriptionDecorator* next = prev->getPlugDescription();

		while( next && next->getDecoratorSortOrder() < p_decorator->getDecoratorSortOrder() )
		{
			prev = next;
			next = next->getPlugDescription();
			assert( next->getDecoratorSortOrder() != p_decorator->getDecoratorSortOrder() && "sort order must be unambiguous" );
		}

		p_decorator->setPlugDescription( next );
		prev->setPlugDescription( p_decorator );
	}

	return p_decorator;
}

void CPlug4::RemoveDecorator( IPlugDescriptionDecorator* decorator )
{
	// traverse decorators
	IPlugDescriptionDecorator* next = this;
	IPlugDescriptionDecorator* pd = next->getPlugDescription();

	while( pd->getPlugDescription() )
	{
		if( pd == decorator )
		{
			next->setPlugDescription( decorator->getPlugDescription() );
			delete decorator;
			return;
		}

		next = pd;
		pd = pd->getPlugDescription();
	}
}

void CPlug4::Notify_GUI( int lHint, void* pHint )
{
	VO_Notify( lHint, pHint );
}

void CPlug4::SetDefaultQuiet(const std::wstring& val)
{
	assert(GetDirection() == DR_IN || isUiPlug() || isSettableOutput()); // can't set output pin default.

	m_default = val; // todo no notifications from State class?
}

void CPlug4::SetDefault(const std::wstring& val)
{
	assert(GetDirection() == DR_IN || isUiPlug() || isSettableOutput()); // can't set output pin default.

	// get InterfaceObject
	auto ultimate = getPlugDescription();
	while (ultimate->getPlugDescription())
		ultimate = ultimate->getPlugDescription();
	auto desc = dynamic_cast<InterfaceObject*>(ultimate);

	if (areEquivalentDefaults(val, (std::wstring) m_default, getDatatype()))
		return;

	m_default = val;

	// only inputs store the default value.  Outputs are under control of module.
	// exception is SDK 2 modules
	if(isUiPlug() && (GetDirection() == DR_IN || isSettableOutput()) )
	{
		if (auto c = UG()->Container(); c) // avoid crash on Main container.
			c->NotifyAllViews2(OM_GUI_PLUG_DEFAULT_CHANGE, 0);
	}

	UG()->OnPlugDefaultChange(this);
	UG()->Document()->SetModified();
}

void CPlug4::SetUG(CUG* p_ug)
{
	assert( p_ug ); // must not be zero.
	cug_ = p_ug;
}

TiXmlElement* CPlug4::ExportXml()
{
	auto plugElement = getPlugDescription()->ExportXml(this);

	// Default applied only if nothing connected.
	// not 100% accurate, e.g. connection to missing SEM. In which case plug reverts to pin-description default.
	// But not going to sound correct if sem missing either.
	if (SetsOwnValue() && !isParameterPlug())
	{
		if (!plugElement)
			plugElement = new TiXmlElement("Plug");

		plugElement->SetAttribute("Default", WStringToUtf8((std::wstring) m_default));
	}

	return plugElement;
}

void CPlug4::Export(IPlug* self, Json::Value& object_json, int targetType)
{
	getPlugDescription()->Export(self, object_json, targetType);

	// Default applied only if nothing connected.
	// not 100% accurate, e.g. connection to missing SEM. In which case plug reverts to pin-description default.
	// But not going to sound correct if sem missing either.
	if (SAT_SYNTHEDIT_GUI_PANEL == targetType || SAT_SYNTHEDIT_GUI_STRUCT == targetType || SAT_SUBCONTROLS_GUI == targetType || SAT_CADMIUM_VIEW == targetType) // Used only for setting GUI pin defaults. Causes assertion if used on DSP pins.
	{
		if (self->SetsOwnValue())
			object_json["default"] = WStringToUtf8((std::wstring) m_default);
	}
}
