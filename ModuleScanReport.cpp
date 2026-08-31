#include "ModuleScanReport.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace
{
	// The button set is the low nibble of the message-box flags, as in Win32
	// (MB_TYPEMASK). MB_OK is 0: a prompt with nothing to answer.
	constexpr int buttonSetMask = 0x0F;
	constexpr int buttonSetOk = 0;

	// The name to show for a module identified only by the file it lives in.
	// "Ambience.sem" and "Ambience.gmpi" are both the module 'Ambience' to a user
	// looking for something to remove or repair.
	std::wstring nameFromPath(const std::filesystem::path& file)
	{
		auto stem = file.stem().wstring();
		return stem.empty() ? file.wstring() : stem;
	}

	const wchar_t* const rule =
		L"--------------------------------------------------------------------------------";
}

ModuleScanReporter& ModuleScanReporter::instance()
{
	static ModuleScanReporter obj;
	return obj;
}

void ModuleScanReporter::begin()
{
	problems_.clear();
	subjectName_.clear();
	subjectPath_.clear();
	collecting_ = true;
}

std::vector<ModuleScanProblem> ModuleScanReporter::end()
{
	collecting_ = false;
	subjectName_.clear();
	subjectPath_.clear();
	return std::exchange(problems_, {});
}

bool ModuleScanReporter::collect(const wchar_t* text, const wchar_t* title, int flags)
{
	if (!collecting_)
		return false;

	if ((flags & buttonSetMask) != buttonSetOk)
		return false; // asks a question -- the caller is waiting on the answer.

	problems_.push_back({
		  subjectName_.empty() ? std::wstring(L"(unidentified module)") : subjectName_
		, subjectPath_
		, title ? title : L""
		, text ? text : L""
		});

	return true;
}

ModuleScanReporter::SubjectScope::SubjectScope(const std::filesystem::path& file)
	: SubjectScope(nameFromPath(file), file.wstring())
{
}

ModuleScanReporter::SubjectScope::SubjectScope(std::wstring name, std::wstring file)
{
	auto& r = ModuleScanReporter::instance();
	previousName_ = std::exchange(r.subjectName_, std::move(name));
	previousPath_ = std::exchange(r.subjectPath_, std::move(file));
}

ModuleScanReporter::SubjectScope::~SubjectScope()
{
	auto& r = ModuleScanReporter::instance();
	r.subjectName_ = std::move(previousName_);
	r.subjectPath_ = std::move(previousPath_);
}

std::vector<std::wstring> moduleScanProblemNames(const std::vector<ModuleScanProblem>& problems)
{
	std::vector<std::wstring> names;

	for (const auto& p : problems)
	{
		// First-seen order, not sorted: the scan walks the modules folder in the
		// order the user will see them, and a linear scan over a handful of names
		// is not worth a set.
		if (std::find(names.begin(), names.end(), p.moduleName) == names.end())
			names.push_back(p.moduleName);
	}

	return names;
}

std::wstring formatModuleScanSummary(const std::vector<ModuleScanProblem>& problems, size_t maxNames)
{
	if (problems.empty())
		return {};

	const auto names = moduleScanProblemNames(problems);

	std::wostringstream oss;

	oss << problems.size() << (problems.size() == 1 ? L" problem" : L" problems")
		<< L" found in " << names.size() << (names.size() == 1 ? L" module" : L" modules")
		<< L" during the module scan:\n";

	const auto shown = (std::min)(names.size(), maxNames);
	for (size_t i = 0; i != shown; ++i)
		oss << L"\n    " << names[i];

	if (shown < names.size())
		oss << L"\n    ...and " << (names.size() - shown) << L" more";

	return oss.str();
}

std::wstring formatModuleScanReport(const std::vector<ModuleScanProblem>& problems)
{
	const auto names = moduleScanProblemNames(problems);

	std::wostringstream oss;

	oss << L"SynthEdit module scan report\n"
		<< L"============================\n\n"
		<< problems.size() << (problems.size() == 1 ? L" problem in " : L" problems in ")
		<< names.size() << (names.size() == 1 ? L" module.\n" : L" modules.\n");

	int index = 0;
	for (const auto& p : problems)
	{
		oss << L"\n\n" << rule << L"\n"
			<< ++index << L" of " << problems.size() << L"  -  " << p.moduleName;

		// The caption is worth keeping only when it says something the body does
		// not; most of these carry the bare app name.
		if (!p.title.empty() && p.title != L"SynthEdit")
			oss << L"  [" << p.title << L"]";

		oss << L"\n";

		if (!p.sourcePath.empty())
			oss << p.sourcePath << L"\n";

		oss << rule << L"\n" << p.detail << L"\n";
	}

	return oss.str();
}
