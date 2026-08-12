
#include "./CLine2.h"
#include "./PlugIO4.h"
#include "./CUG.h"
#include "./CContainer.h"
#include "it_plug_destinations.h"
#include "UgDatabase.h"
#include "SynthEditDocBase.h"
#include "../tinyXml2/tinyxml2.h"
#include "Notify_msg.h"

CPlugIO4::CPlugIO4( CUG* p_ug, InterfaceObject* i ) : CPlug4(p_ug, i)
	,TiedTo( 0 )
	,m_direction( DR_IN )
	,m_datatype( DT_FSAMPLE )
{
}

CPlugIO4::~CPlugIO4()
{
	if( TiedTo ) // avoid virtual function... GetTiedTo() != nullptr )
	{
		TiedTo->TiedTo = nullptr;
		TiedTo = nullptr;
	}
}

void CPlugIO4::Export(Json::Value& object_json, int targetType)
{
	CPlug4::Export(object_json, targetType);

	// highlist IO plugs with only one size connected
	if (targetType == SAT_SYNTHEDIT_GUI_STRUCT && TiedTo && !TiedTo->HasActiveConnections())
	{
		object_json["partial"] = true;
	}
}

void CPlugIO4::Export(tinyxml2::XMLElement* pinE, ExportFormatType targetType)
{
	CPlug4::Export(pinE, targetType);

	if (m_direction != DR_IN)
	{
		pinE->SetAttribute("direction", "1");
	}
	if (m_datatype != DT_FSAMPLE)
	{
		pinE->SetAttribute("datatype", m_datatype);
	}

	// Only need to serialise on one side
	if (TiedTo && UG()->GetFlags() & CF_IO_MOD)
	{
		pinE->SetAttribute("tiedtomod", TiedTo->UG()->Handle());
		pinE->SetAttribute("tiedtopin", TiedTo->UG()->getPinNumber(TiedTo));
	}
}

void CPlugIO4::Import(tinyxml2::XMLElement* pinE, ExportFormatType targetType)
{
	CPlug4::Import(pinE, targetType);

	// get datatype, direction , tied-to
	int dir = DR_IN;
	if (tinyxml2::XML_SUCCESS == pinE->QueryAttribute("direction", &dir) && dir == 1)
	{
		m_direction = DR_OUT;
	}

	int datatype = DT_FSAMPLE;
	if (tinyxml2::XML_SUCCESS == pinE->QueryAttribute("datatype", &datatype))
	{
		m_datatype = (EPlugDataType) datatype;
	}

	int tiedto_handle = -1;
	int tiedto_pin = -1;
	if (tinyxml2::XML_SUCCESS == pinE->QueryAttribute("tiedtomod", &tiedto_handle) && tinyxml2::XML_SUCCESS == pinE->QueryAttribute("tiedtopin", &tiedto_pin))
	{
//		auto mate = dynamic_cast<CUG*>(UG()->Document()->uniqueIdDatabase.HandleToObject(tiedto_handle));
		// tiedto module must be own container.
		auto mate = UG()->Container();
		assert(mate->Handle() == tiedto_handle);
		auto tiedto = mate->GetPlug(tiedto_pin);
		TiedTo = dynamic_cast<CPlugIO4*>(tiedto);
		TiedTo->TiedTo = this;
	}
}

#if 0 // was a pain
//void CPlugIO4::Post Load()
void CPlugIO4::Initialise( bool loaded_from_file ) //Post Load()
{
	CPlug4::Initialise(loaded_from_file);
	// Highlight lines going into container, but with nothing connected on inside. (can cause voice sleeping issues).
	if( loaded_from_file )
	{
		if( GetDirection() == DR_IN )
		{
			auto outsidePlug = GetTiedTo();
			if (outsidePlug && outsidePlug->GetNumConnections() == 0 )
			{
				for (auto& l : Connectors() )
				{
					l->HighlightLineTo(l->ToPlug->UG());
				}
			}
		}
	}
}
#endif

// For SPARE plugs, determine where they go (either into or outof a container)
// and set 'Tied to' member of both
CPlugIO4* CPlugIO4::AutoTie(EDirection d)
{
	CUG* TiedToUG = nullptr;

	if( UG()->GetFlags() & CF_IO_MOD )
	{
		TiedToUG = Container();
	}
	else
	{
		TiedToUG = ((CContainer*)UG())->GetIoModule( d );
	}

	CPlugIO4* TiedtoPlug = TiedToUG->GetSparePlug();
	TiedTo = TiedtoPlug;
	TiedtoPlug->TiedTo = this;
	return TiedtoPlug;
}

bool CPlugIO4::isIoPlug()
{
	return true;
}

bool CPlugIO4::isUnconnectedIOPlug()
{
	if( GetTiedTo() == 0 )
		return true;

	return !GetTiedTo()->HasActiveConnections();
}

bool CPlugIO4::isUnusedSpare()
{
	// during connection to an IO mod, TiedTo will be set, but will have no connections.
	return GetNumConnections() == 0 && (GetTiedTo() == 0 || GetTiedTo()->GetNumConnections() == 0);
}

void CPlugIO4::OnRemove()
{
	IPlug* p2= GetTiedTo();

	if( p2 != 0 )
	{
		// should never happen (plug without parent pointer), but seems posible
		// results in memory leak
		assert( p2->UG() != 0 ); // has happened, to rurik
		((CPlugIO4*)p2)->SetTiedTo(nullptr);	// Prevent infinite recursion
		SetTiedTo(nullptr);	// Prevent removeline removing this (again, recursively)
		CUG* tied_to_parent = p2->UG();

		if( tied_to_parent ) // has happened, to rurik
		{
			tied_to_parent->RemovePlug(p2);

			if( tied_to_parent->Container() )
			{
				tied_to_parent->Container()->VO_Notify( OM_LAYOUT_CHANGE2, tied_to_parent );
			}
		}
	}
}

std::wstring CPlugIO4::getFileExt()
{
	EDirection searchDirection = DR_IN;

	if( isUiPlug() ) // assume it's an SDK2 GUI plug. (GUI list pins not supported on SDK3).
	{
		searchDirection = DR_OUT; // GUI Pins reversed.
	}

	IPlug* connected_to = GetUltimateDest2( searchDirection );

	if( connected_to )
	{
		return connected_to->getFileExt();
	}

	return {};
}

std::wstring CPlugIO4::getDefaultEnumList()
{
	EDirection searchDirection = DR_IN;

	if( isUiPlug() ) // assume it's an SDK2 GUI plug. (GUI list pins not supported on SDK3).
	{
		searchDirection = DR_OUT; // GUI Pins reversed.
	}

	IPlug* connected_to = GetUltimateDest2( searchDirection );

	if( connected_to )
	{
		return connected_to->getDefaultEnumList();
	}

	return (L"");
}

sRange CPlugIO4::GetDefaultRange()
{
	IPlug* connected_to = GetUltimateDest2();

	if( connected_to )
	{
		return connected_to->getDefaultEnumList();
	}

	return sRange();
}

IPlug* CPlugIO4::GetUltimateDest2(EDirection dr)
{
	if( GetDirection() == dr )
	{
		if( GetTiedTo() )
			return GetTiedTo()->GetUltimateDest2( dr );
	}
	else
	{
		if (!ConnectedTo())
			return {};

#ifdef _DEBUG
		auto oldway = ConnectedTo()->GetUltimateDest2( dr );
#endif

		IPlug* newway{};

		it_plug_destinations it(this);
		it.First();
		if (!it.IsDone())
			newway = it.CurrentItem();

		assert(newway == oldway); // should generally agree, but newway should work better at avoiding dead-ends.

		return newway;
	}

	return {};
}

void CPlugIO4::OnNewConnection(CLine2* p_line)
{
	const bool is_first_connection = isUnusedSpare();

	const bool was_unconnected_on_this_side = GetNumConnections() == 0 && TiedTo && TiedTo->HasActiveConnections();

	if( is_first_connection )
	{
		// get what we are now connected to
		IPlug* standard_plug = p_line->FromPlug;

		if( standard_plug == this )
		{
			standard_plug = p_line->ToPlug;
		}

		// Spare plugs must be renamed and have direction set
		CPlugIO4* tied_to = AutoTie( standard_plug->GetDirection() ); // the matching plug on IO mod
		SetDatatype( standard_plug->getDatatype() );
		tied_to->SetDatatype( standard_plug->getDatatype() );

		if( standard_plug->GetDirection() == DR_IN )
		{
			SetDirection(DR_OUT);
			tied_to->SetDirection(DR_IN);
			tied_to->SetDefault( standard_plug->GetDefault() );
		}
		else
		{
			SetDirection(DR_IN);
			tied_to->SetDirection(DR_OUT);
		}

		setName( standard_plug->getName() );
	}

	CPlug4::OnNewConnection(p_line);

	if( is_first_connection )
	{
		GetTiedTo()->UG()->AddPlug( GetTiedTo()->Duplicate() );
		// this must be done after base class adds connector pointer, so ug can find it's data type and set color correctly
		GetTiedTo()->setName( getName() );
	}

	// plug changes to show that other side is now connected
	if (was_unconnected_on_this_side)
	{
		auto otherUgsContainer = GetTiedTo()->UG()->Container();
		otherUgsContainer->VO_Notify(OM_REFRESH_PRESENTERS, otherUgsContainer);
	}
}

void CPlugIO4::Disconnect_pt2(CLine2* p_line)
{
	const bool willLeaveOtherEndAlone = GetNumConnections() == 0 && TiedTo && TiedTo->HasActiveConnections();

	CPlug4::Disconnect_pt2(p_line);

	// plug changes to show that other side is now unconnected
	if (willLeaveOtherEndAlone)
	{
		auto otherUgsContainer = GetTiedTo()->UG()->Container();
		otherUgsContainer->VO_Notify(OM_REFRESH_PRESENTERS, otherUgsContainer);
	}
}

void CPlugIO4::PropogateBack(IPlug* plug, int msg_id)
{
	if( GetDirection() == DR_IN )
	{
		CPlug4::PropogateBack( plug, msg_id);
		/* why???
		if( plug != this )
		{
			switch( msg_id == OM_DOWNSTREAM_PLUG_ENUM_CHANGE)
			{
				UG()->OnDownstream EnumChange(); // let ug know
		}*/
	}
	else
	{
		assert( GetTiedTo()->GetDirection() == DR_IN && "recursive situation, IO pin directions inconsistant");
		GetTiedTo()->PropogateBack( plug,msg_id);
	}
}

void CPlugIO4::PropogateForward(IPlug* plug, int msg_id)
{
	if( GetDirection() == DR_OUT )
	{
		CPlug4::PropogateForward( plug, msg_id);
		/* why???
		if( plug != this )
		{
			switch( msg_id == OM_DOWNSTREAM_PLUG_ENUM_CHANGE)
			{
				UG()->OnDownstream EnumChange(); // let ug know
		}*/
	}
	else
	{
		if( GetTiedTo() )
			GetTiedTo()->PropogateForward( plug,msg_id);
	}
}

bool CPlugIO4::canRemove()
{
	return GetNumConnections() == 0 && ( GetTiedTo() == 0 || GetTiedTo()->GetNumConnections() == 0);
}

bool CPlugIO4::isUiPlug()
{
	/*
		if( GetTiedTo() ) // GetUltimateDest2 only works on non-spare.
		{
			// find the plug this ultimately connects to
			IPlug *connected_to = GetUltimate Dest2();

			// is it annother container plug (not much use)
			if( connected_to )
			{
				return connected_to->isUiPlug();
			}
		}

		return false;
	*/
	// MIGHT BE WAY FASTER TO STORE A FLAG.
	// get a 'real' plug connected downstream or upstream.
	IPlug* ultimatePlug = 0;
	it_plug_destinations it( this );
	it.First();

	if( !it.IsDone() )
	{
		ultimatePlug = it.CurrentItem();
	}
	else
	{
		// try other direction.
		if( GetTiedTo() )
		{
			it_plug_destinations it2( GetTiedTo() );
			it2.First();

			if( !it2.IsDone() )
			{
				ultimatePlug = it2.CurrentItem();
			}
		}
	}

	return ultimatePlug && ultimatePlug->isUiPlug();
}

void CPlugIO4::setName( const std::wstring& p_name)
{
	getPlugDescription()->setName(this, p_name);

	if( GetTiedTo() )
	{
		if( GetTiedTo()->getName() != p_name )
		{
			GetTiedTo()->setName( p_name );
		}
	}
}

void CPlugIO4::highlightOutsideLines2(int highlightType)
{
	auto outsidePin = GetTiedTo();
	if (outsidePin)
	{
		auto outsideUg = outsidePin->UG();
		outsideUg->HighlightLines( outsideUg->getPlugIdx(outsidePin), highlightType);
	}
}

