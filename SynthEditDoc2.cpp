#include "SynthEditDoc2.h"
#include "Application.h"
//#include "version.h"
#include "Notify_msg.h"

SynthEditDoc::SynthEditDoc() : CSynthEditDocBase()
{
}

bool SynthEditDoc::OnOpenDocument(const wchar_t* lpszPathName)
{
	return (bool)OnOpenDocument2(lpszPathName);
}

void SynthEditDoc::DeleteContents()
{
	CSynthEditDocBase::DeleteContents();
//	CDocument::DeleteContents();
}

void SynthEditDoc::OnProperties()
{
	assert(false); // TODO
#if 0
	dlg_doc_prop Dlg;
	Dlg.Doc = this;
	Dlg.DoModal();
#endif
}

bool SynthEditDoc::OnSaveDocument(const wchar_t* lpszPathName)
{
	// TODO
	//auto app = (CSynthEditApp*)Application();
	//app->WriteProfileString(SYNTHEDIT_REGISTRY_KEY, L"LastFile", lpszPathName);
	
	return (bool) OnSaveDocument2(lpszPathName);
}

void SynthEditDoc::SetModified(bool p_modified_flag)
{
	isModified_ = p_modified_flag;
}

bool SynthEditDoc::doCloseDoc()
{
	/* TODO
	if (!SaveModified())
		return true;	// document can't close right now -- don't close it
	*/

	Application()->CloseAllViews();

	return false; // OK
}



