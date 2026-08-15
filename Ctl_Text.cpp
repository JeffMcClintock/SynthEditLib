#include "Ctl_Text.h"
#include "Notify_msg.h"
#include "SkinMgr.h"
#include "UgDatabase.h"
#include "tinyxml/tinyxml.h"
#include "modules/se_sdk3_hosting/GmpiResourceManager.h"
#include "it_plug_destinations.h"

#define PN_OUTPUT			6

Ctl_Text::Ctl_Text( Module_Info* p_type ) : CControl( p_type )
	,FileExt(L"xxx") // needed to trigger browse button deletion on disconnect
{
}

TiXmlElement* Ctl_Text::ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType )
{
	TiXmlElement* module_element = CUG::ExportXml(XmlParent, targetType );

	if (module_element == nullptr || targetType != SAT_SUBCONTROLS_GUI )
	{
		return module_element;
	}

	module_element->SetAttribute( "Type", "TextEntryV" );

	return module_element;
}

void Ctl_Text::Export(Json::Value& module_element, ExportFormatType targetType)
{
	assert(targetType == SAT_SYNTHEDIT_GUI_STRUCT || targetType == SAT_SYNTHEDIT_DOCUMENT || targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL); // else update if conditions.

	CUG::Export(module_element, targetType);

	module_element["type"] = "SE Text Entry";
	FlagRequiredModuleForExport(L"SE Text Entry");

	if (show_title_on_panel())
	{
		Json::Value pin_element2(Json::objectValue);
		pin_element2["default"] = WStringToUtf8(GetName());
		pin_element2["Id"] = 12;
		module_element["Pins"].append(pin_element2);
	}

	if (targetType == SAT_SUBCONTROLS_GUI)
	{
		// Ensure font-data exported.
		gmpi_sdk::MpString returnUri;
		const char* imagename("browse");
		GmpiResourceManager::Instance()->RegisterResourceUri(Handle(), currentVst3SkinName, imagename, "Image", &returnUri); // resource leak, but OK for a hack.
		GmpiResourceManager::Instance()->RegisterResourceUri(Handle(), currentVst3SkinName, imagename, "ImageMeta", &returnUri);
	}
}

bool Ctl_Text::needBrowseButton()
{
	// Does this display a filename, if so show browse button.
	it_plug_destinations it( GetPlug(L"Text Out") );
	it.First();

	if( !it.IsDone() )
	{
		return it.CurrentItem()->is_filename();
	}

	return false;
}

void Ctl_Text::SetFileExt(const std::wstring& p_file_ext)
{
	// If nesc update window (by closing and re-opening)
	if( FileExt != p_file_ext )
	{
		FileExt = p_file_ext;
		LayoutChange();
	}
}

void Ctl_Text::OnDownstreamPlugChange(IPlug* p_my_plug, IPlug* p_downstream_plug, int p_msg_id)
{
	CControl::OnDownstreamPlugChange(p_my_plug, p_downstream_plug, p_msg_id);

	switch( p_msg_id )
	{
	case OM_DOWNSTREAM_PLUG_CONNECT2:
		if( p_my_plug == GetPlug(PN_OUTPUT) )
		{
			// may need to add file browser.
			LayoutChange();
		}

		break;
	}
}

/* NOTES
At this point I've had another problem.
How can I stop editing??
I've assumed that the ending of editing is marked from the pressure of
the enter key, so I've trapped the PreTranslateMessage of the CView to
hide the CEdit control, but this is not a good solution, because I can
not trap the click of the mouse outside the control, or other event like
TAB key, and so on.
So, if you, or anyone,  have any suggestion for this, I'll appreciate
very much.

  bool CFunziView::PreTranslateMessage(MSG* pMsg)
{
		// TODO: Add your specialized code here and/or call the base class

		// Trapping the Enter Key on the m_ctlEdit
		if (pMsg->message == WM_KEYDOWN  &&  ((int)pMsg->wParam) == VK_RETURN)
		{
				std::wstring text = "";
				m_ctlEdit.GetWindowText( text );
				m_pEditing->SetText( text );
				m_ctlEdit.ShowWindow( SW_HIDE );

				m_pEditing == nullptr;
				m_fntEdit.DeleteObject();

				SetFocus();
				Invalidate();
				return TRUE;
		}
		else
				return CScroll  View::PreTranslateMessage( pMsg );
}
--------------------------------------------------------------------
>To refresh everyone's memory: The original problem was that of displaying a
>Multiline Edit Control in a window, and wanting to let the user press Enter
>to do something like leave the window, or whatever.  The problem is that
>the Edit Control "sucks up" the Enter keypress, and the parent window never
>sees it.
>
>Any better ideas out there?  Hopefully simple ones that work without
>needing MFC.

Well maybe, but it does use MFC and I don't know about multiline
problems.  But ignorance be ignored.  How about:

bool CMyEditClass::PreTranslateMessage(MSG* pMsg)
{

		if (pMsg->message == WM_KEYDOWN)
		{
				::TranslateMessage(pMsg);
				::DispatchMessage(pMsg);
				return TRUE;
		}
		return CEdit::PreTranslateMessage(pMsg);
}

void CMyEditClass::On Char(UINT nChar, UINT nRepCnt, UINT nFlags)
{
..
		if (nChar == VK_RETURN)
		{
				// do enter thing -  set focus to next control
				GetParent()->GetNextDlgTabItem(this)->SetFocus();
		}
..
}
------------------------------------------------------------------
> I try to create a CEdit control that processes WM_CHAR, VK_RETURN
> message itself ( using VC++ 5.0).
> I tried out message reflection and overwriting the edit's  windowproc,
> but it doesn't work.
> Does anybody know, how I can get the VK_RETURN  into the edit's  OnChar
> function ??
>
> Thanks for your help !
> Frank Keck

The code example at: http//home.earthlink.net/~railro/example5.zip
demonstrates how to subclass the CEdit control to handle the VK_RETURN.

You have to overide

UINT CExampleEdit::OnGetDlgCode()
{
		return  CEdit::OnGetDlgCode()|DLGC_WANTMESSAGE;
}

but then you also have to handle some normal work of the CEdit control
yourself:

void CExampleEdit::On Char(UINT nChar, UINT nRepCnt, UINT nFlags)
{
		if (nChar == VK_TAB)    // Handle the TAB key
				{
				if (::GetKeyState(VK_SHIFT) < 0)
						{
						(CDialog *)GetParentOwner()->GetNextDlgTabItem(this,
TRUE)->SetFocus();
						}
				else
						{
						(CDialog *)GetParentOwner()->GetNextDlgTabItem(this)->SetFocus();
						}

				return;
				}

		if (nChar == VK_RETURN)
				{
				// Do whatever you want to do here... how about tab to the next
control:

				(CDialog *)GetParentOwner()->GetNextDlgTabItem(this)->SetFocus();

				return;
				}

		if (nChar == VK_ESCAPE)
				{
				//  Need to cancel the dialog
				(CDialog *)GetParentOwner()->SendMessage(WM_CLOSE);
				return;
				}

		CEdit::On Char(nChar, nRepCnt, nFlags);
}

BTW, to use the control, all you have to do is include the header file
and create a variable of the new class:

// Dialog Data
		/./.{.{AFX._DATA(CExampleDlg)
		enum { IDD = IDD_EXAMPLE };
		CExampleEdit    m_CtrlEdit2;
		CExampleEdit    m_CtrlEdit1;
		std::wstring m_szValue1;
		std::wstring m_szValue2;
		/../}.}.AFX._DATA

There is no need to use SubclassDlgItem().


  */

