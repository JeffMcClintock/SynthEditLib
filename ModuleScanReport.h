#pragma once

#include <filesystem>
#include <string>
#include <vector>

// ONE DIALOG PER SCAN, NOT ONE PER PROBLEM.
//
// A rescan across a folder of third-party modules can raise dozens of message
// boxes -- a module ID found twice here, a malformed pin attribute there -- and
// every one of them is modal. The scan stalls until somebody clicks OK, so the
// user's only route through a bad modules folder is forty clicks, by which point
// the first message has long since scrolled out of memory and none of them can be
// compared against each other.
//
// So while a scan is running the message boxes are COLLECTED rather than shown,
// and the scan ends with a single dialog naming the modules involved plus an offer
// to open the full text. Module_Info::flushPinXmlDiagnostics already does exactly
// this one level down -- all of one module's pin problems into one dialog -- and
// this is the same idea one level up.

struct ModuleScanProblem
{
	std::wstring moduleName;	// what to call it in the summary list
	std::wstring sourcePath;	// file it came from; empty when not known
	std::wstring title;			// caption the suppressed dialog carried
	std::wstring detail;		// its full text
};

// Collects the problems raised while a module scan is running.
//
// A process-wide singleton because the reporting sites are scattered over three
// libraries and all reach the dialog through SafeMessagebox(), a free function
// with nowhere to thread a context argument through -- the same reason
// CModuleFactory and GmpiResourceManager are singletons. Scans are not concurrent.
class ModuleScanReporter
{
public:
	static ModuleScanReporter& instance();

	// begin() discards anything left over from an abandoned scan; end() stops
	// collecting and hands over what was gathered.
	void begin();
	std::vector<ModuleScanProblem> end();

	bool isCollecting() const { return collecting_; }

	// Offer a message box to the collector. Returns true if it was taken, in which
	// case the caller must NOT show it.
	//
	// ONLY OK-ONLY PROMPTS ARE TAKEN. A prompt that asks a question has a caller
	// waiting on the answer, and answering it silently on the user's behalf is a
	// different and much worse bug than too many dialogs. Nothing on the scan path
	// asks a question today; this is what keeps that true if something starts to.
	bool collect(const wchar_t* text, const wchar_t* title, int flags);

	// Names the module that subsequent problems belong to.
	//
	// RAII because most of the error paths in the scanner `return` the moment they
	// report -- an assignment-and-restore pair would be skipped by every one of them.
	class SubjectScope
	{
	public:
		// The usual case: the file being scanned. Its stem is the module's name.
		explicit SubjectScope(const std::filesystem::path& file);

		// For when the scan has read a better name out of the module's own XML --
		// a shell plugin's sub-plugins are not named after the bundle holding them.
		SubjectScope(std::wstring name, std::wstring file);

		~SubjectScope();

		SubjectScope(const SubjectScope&) = delete;
		SubjectScope& operator=(const SubjectScope&) = delete;

	private:
		std::wstring previousName_;
		std::wstring previousPath_;
	};

private:
	// SubjectScope reaches the members below directly: a nested class is granted
	// access to its enclosing class' privates, no friend declaration needed.
	bool collecting_ = false;
	std::wstring subjectName_;
	std::wstring subjectPath_;
	std::vector<ModuleScanProblem> problems_;
};

// The distinct module names, in the order first seen. Several problems in one
// module is the common case (a bad XML file fails per sub-plugin), and the user
// wants a list of things to go and fix, not a list of events.
std::vector<std::wstring> moduleScanProblemNames(const std::vector<ModuleScanProblem>& problems);

// The summary dialog's text: a count and the module names, nothing else. Capped,
// because a folder with 200 broken modules must still produce a dialog that fits
// on the screen -- the report file is where the rest lives.
std::wstring formatModuleScanSummary(const std::vector<ModuleScanProblem>& problems, size_t maxNames = 12);

// The full report: every problem, in full, with the file that produced it.
std::wstring formatModuleScanReport(const std::vector<ModuleScanProblem>& problems);
