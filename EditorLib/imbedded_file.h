// imbedded_file.h: interface for the imbedded_file class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_IMBEDDED_FILE_H__4642DC91_EB8F_11D6_AFF7_000103170662__INCLUDED_)
#define AFX_IMBEDDED_FILE_H__4642DC91_EB8F_11D6_AFF7_000103170662__INCLUDED_

#pragma once

#include <string>
#include <filesystem>

class imbedded_file
{
public:
	imbedded_file(class ApplicationBase* application = 0, const std::wstring& p_filename = {}, const std::wstring& temp_path = {});

	std::wstring Unload(const std::filesystem::path& p_unload_path);
	std::wstring Filename()
	{
		return filename;
	}
	size_t Size()
	{
		return data.size();
	}

	std::wstring macSourceFilename;

protected:
	std::wstring filename;
	std::string data;
};

#endif // !defined(AFX_IMBEDDED_FILE_H__4642DC91_EB8F_11D6_AFF7_000103170662__INCLUDED_)
