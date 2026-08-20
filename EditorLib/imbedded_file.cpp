#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <assert.h>
#include "imbedded_file.h"
#include "Application.h"
#include "conversion.h"

using namespace std;

imbedded_file::imbedded_file(ApplicationBase* application, const std::wstring& unload_filename, const std::wstring& temp_file_path ) :
	filename(unload_filename)
{
	if( unload_filename.empty() ) // creation b4 serialise
		return;

	std::wstring actual_path = temp_file_path;

	if( actual_path.empty() ) // SEM3 copied to temp file first.
		actual_path = unload_filename;

	// resolve partial filenames (if object hapens to be in Synthedit folder or child.)
	const std::wstring full_filename = application->ResolveFilename( actual_path, L"*" );
    const auto full_filenameU = WStringToUtf8(full_filename);
	std::ifstream t(full_filenameU, std::ifstream::in | std::ifstream::binary);

	if (!t.is_open())
	{
		assert(false); // imbedded file not found. Virus checker might have deleted it.
		return;
	}

	t.seekg(0, std::ios::end);
	const size_t size = t.tellg();
	data.resize(size);
	t.seekg(0);
	t.read((char*)data.data(), data.size());
}

std::wstring imbedded_file::Unload(const std::filesystem::path& p_unload_path)
{
	const std::wstring new_filename = StripPath(filename);
    const auto new_path = (p_unload_path / new_filename).generic_string();

	ofstream f(new_path, ios_base::out | ios_base::binary);

	f.write((char*)data.data(), data.size());

	return Utf8ToWstring(new_path);
}

