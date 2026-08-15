
#include "Control.h"
#include "CContainer.h"
#include "Notify_msg.h"
#include "UgDatabase.h"

using namespace std;

std::string CControl::currentVst3SkinName;

CControl::CControl( Module_Info* p_type ) : CUG( p_type )
	,PanelWndPosition(0,0,0,0) // new objects only size correctly if PanelWndPosition empty see control_frame_grab::SeMeasure()
{
}

void CControl::OnPlugDefaultChange(IPlug* plug)
{
	CUG::OnPlugDefaultChange(plug);	// Call base class
	std::wstring n = plug->getName();

	if( n.compare(L"Appearance") == 0 || n.compare(L"Show Readout") == 0 || n.compare(L"Show Title On Panel") == 0 || n.compare(L"Hint") == 0 )
	{
		LayoutChange();		// update window (by closing and re-opening)
	}
}

void CControl::SetName(const std::wstring& new_name)
{
	CUG::SetName(new_name);
	LayoutChange();
}

// recalc the layout of control and sub-windows
// done by closing then re-opening
void CControl::LayoutChange()
{
	// can't do via direct notification, e.g layoutchange may delete editbox, which may be next on notify list
	if( Container() )
	{
		Container()->VO_Notify( OM_LAYOUT_CHANGE2, this );
	}
}

gmpi::drawing::RectL CControl::getViewObRect(int p_view_type)
{
	if( p_view_type == CF_PANEL_VIEW )
		return PanelWndPosition;
	else
		return CUG::getViewObRect(p_view_type);
}

void CControl::setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect)
{
	if( p_view_type == CF_PANEL_VIEW )
	{
		if( PanelWndPosition != p_rect )
		{
			PanelWndPosition = p_rect;

			if (Container()) // main view has null container
			{
				Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_PANEL, (void*)this);
			}
		}
	}
	else
		CUG::setViewObRect( p_view_type, p_rect );
}

bool CControl::show_title_on_panel()
{
	// Put title on all Older Panel objects by default.
	IPlug* p = GetPlug( (L"Show Title On Panel") );
	return p == 0 || p->GetDefault().compare(L"1") == 0;
}

// !!! replace getShortDescription globally with getHint ???
std::wstring CControl::getShortDescription()
{
	IPlug* p = GetPlug(L"Hint");

	if (p)
	{
		return p->GetDefault();
	}

	return L""; //getHint();
}