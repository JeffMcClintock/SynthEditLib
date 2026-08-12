#pragma once
#include "SynthEditDocBase.h"

class SynthEditDoc : public CSynthEditDocBase
{
public:
	SynthEditDoc();
	virtual ~SynthEditDoc() {}
	void DeleteContents() override;

	bool OnOpenDocument(const wchar_t* lpszPathName);
	bool OnSaveDocument(const wchar_t* lpszPathName) override;

	bool doCloseDoc() override;
	void SetModified(bool p_modified_flag = true) override;
	
	void setPathName(const std::wstring& p_path_name) override
	{
		pathName = p_path_name;
//		OnDocumentEvent(onAfterSaveDocument);
	}
	void OnProperties() override;
};

