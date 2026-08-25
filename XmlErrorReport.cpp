#include "XmlErrorReport.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "conversion.h"
#include "modules/tinyXml2/tinyxml2.h"

using namespace tinyxml2;

namespace
{
constexpr auto npos = std::string_view::npos;

// How many lines of XML to show either side of the offending one.
constexpr int contextLines = 2;

// Long lines get truncated rather than turning the dialog into a wall of text.
constexpr size_t maxExcerptLineLength = 200;

// ---------------------------------------------------------------- text utils

// tinyxml2 counts lines by '\n' only, starting at 1. Match that exactly or the
// excerpt we print is not the line it complained about.
std::string_view lineAt(std::string_view text, int lineNum)
{
	if (lineNum < 1)
		return {};

	size_t begin = 0;
	for (int line = 1; line < lineNum; ++line)
	{
		const auto eol = text.find('\n', begin);
		if (eol == npos)
			return {};
		begin = eol + 1;
	}

	auto end = text.find('\n', begin);
	if (end == npos)
		end = text.size();

	// CRLF: the '\r' belongs to the line ending, not to the text.
	if (end > begin && text[end - 1] == '\r')
		--end;

	return text.substr(begin, end - begin);
}

int countLines(std::string_view text)
{
	const auto newlines = static_cast<int>(std::count(text.begin(), text.end(), '\n'));

	// A trailing newline ends the last line, it does not start an empty one -
	// and an empty final row in the excerpt just looks like a truncation bug.
	return (!text.empty() && text.back() == '\n') ? newlines : newlines + 1;
}

int lineOfOffset(std::string_view text, size_t offset) // 1-based, matching tinyxml2
{
	return 1 + static_cast<int>(std::count(text.begin(), text.begin() + (std::min)(offset, text.size()), '\n'));
}

std::string forDisplay(std::string_view line)
{
	std::string out(line.substr(0, (std::min)(line.size(), maxExcerptLineLength)));

	if (line.size() > maxExcerptLineLength)
		out += " ...";

	// Tabs would be rendered at whatever width the dialog fancies; a space is
	// predictable and the indent is decoration here anyway.
	std::replace(out.begin(), out.end(), '\t', ' ');

	return out;
}

// ------------------------------------------------------ start-tag re-scanner

enum class TagFaultKind
{
	none,
	missingEquals,			// name not followed by '='
	unquotedValue,			// '=' not followed by a quote
	unterminatedValue,		// opening quote with no closing quote
	duplicateAttribute,		// tinyxml2 reports this as a *parse* error
	unexpectedCharacter,	// junk where an attribute or '>' should be
	unexpectedEnd,			// document ran out mid-tag
};

struct TagFault
{
	TagFaultKind kind = TagFaultKind::none;
	size_t offset = 0;		// into the whole document
	std::string elementName;
	std::string attributeName;
};

// A deliberately small re-implementation of tinyxml2's start-tag parser
// (XMLElement::ParseAttributes / XMLAttribute::ParseDeep). We run it only after
// tinyxml2 has already failed, purely to name the token it choked on: tinyxml2
// reports a line but never says which attribute, nor that "malformed attribute"
// also covers "you wrote the same attribute twice".
//
// Returns the offset just past the tag, or npos if it found a fault.
size_t scanStartTag(std::string_view xml, size_t pos, TagFault& fault, bool* selfClosing = nullptr)
{
	assert(pos < xml.size() && xml[pos] == '<');
	++pos;

	if (selfClosing)
		*selfClosing = false;

	const size_t nameStart = pos;
	while (pos < xml.size() && XMLUtil::IsNameChar(static_cast<unsigned char>(xml[pos])))
		++pos;

	fault.elementName.assign(xml.substr(nameStart, pos - nameStart));

	const auto fail = [&](TagFaultKind kind, size_t at, std::string_view attribute)
		{
			fault.kind = kind;
			fault.offset = (std::min)(at, xml.empty() ? size_t{ 0 } : xml.size() - 1);
			fault.attributeName.assign(attribute);
			return npos;
		};

	std::vector<std::string> seen;

	for (;;)
	{
		while (pos < xml.size() && XMLUtil::IsWhiteSpace(xml[pos]))
			++pos;

		if (pos >= xml.size())
			return fail(TagFaultKind::unexpectedEnd, xml.size(), {});

		if (xml[pos] == '>')
			return pos + 1;

		if (xml[pos] == '/' && pos + 1 < xml.size() && xml[pos + 1] == '>')
		{
			if (selfClosing)
				*selfClosing = true;
			return pos + 2;
		}

		if (!XMLUtil::IsNameStartChar(static_cast<unsigned char>(xml[pos])))
			return fail(TagFaultKind::unexpectedCharacter, pos, {});

		const size_t attrStart = pos;
		while (pos < xml.size() && XMLUtil::IsNameChar(static_cast<unsigned char>(xml[pos])))
			++pos;

		const auto attributeName = xml.substr(attrStart, pos - attrStart);

		while (pos < xml.size() && XMLUtil::IsWhiteSpace(xml[pos]))
			++pos;

		if (pos >= xml.size() || xml[pos] != '=')
			return fail(TagFaultKind::missingEquals, attrStart, attributeName);

		++pos; // past '='

		while (pos < xml.size() && XMLUtil::IsWhiteSpace(xml[pos]))
			++pos;

		if (pos >= xml.size() || (xml[pos] != '"' && xml[pos] != '\''))
			return fail(TagFaultKind::unquotedValue, attrStart, attributeName);

		const char quote = xml[pos];
		const auto close = xml.find(quote, pos + 1);
		if (close == npos)
			return fail(TagFaultKind::unterminatedValue, attrStart, attributeName);

		pos = close + 1;

		// tinyxml2 checks for a repeat only after the value parses, and reports
		// it with the very same error code as a malformed one. Same order here.
		if (std::find(seen.begin(), seen.end(), attributeName) != seen.end())
			return fail(TagFaultKind::duplicateAttribute, attrStart, attributeName);

		seen.emplace_back(attributeName);
	}
}

// What the '<' at pos begins.
struct Markup
{
	enum Kind { startTag, endTag, skipped, stray } kind = stray;
	size_t end = 0;		// offset just past the construct (skipped/stray only)
	bool truncated = false;	// an unterminated comment/CDATA/PI - give up, tinyxml2 says so
};

// Comments, CDATA, processing instructions and DOCTYPEs are stepped over whole,
// so a '<' inside one of them is never mistaken for the start of a tag.
Markup classifyMarkup(std::string_view xml, size_t pos)
{
	const auto skipTo = [&](size_t from, std::string_view terminator) -> Markup
		{
			const auto end = xml.find(terminator, from);
			return end == npos
				? Markup{ Markup::skipped, xml.size(), true }
				: Markup{ Markup::skipped, end + terminator.size(), false };
		};

	if (xml.compare(pos, 4, "<!--") == 0)
		return skipTo(pos + 4, "-->");

	if (xml.compare(pos, 9, "<![CDATA[") == 0)
		return skipTo(pos + 9, "]]>");

	if (xml.compare(pos, 2, "<?") == 0)
		return skipTo(pos + 2, "?>");

	if (xml.compare(pos, 2, "<!") == 0)
		return skipTo(pos + 2, ">");

	if (xml.compare(pos, 2, "</") == 0)
		return { Markup::endTag, pos, false };

	if (pos + 1 < xml.size() && XMLUtil::IsNameStartChar(static_cast<unsigned char>(xml[pos + 1])))
		return { Markup::startTag, pos, false };

	return { Markup::stray, pos + 1, false }; // tinyxml2 will complain about it
}

// Walk the document for the first malformed start-tag.
bool findFirstTagFault(std::string_view xml, TagFault& fault)
{
	size_t pos = 0;
	while ((pos = xml.find('<', pos)) != npos)
	{
		const auto markup = classifyMarkup(xml, pos);

		if (markup.truncated)
			return false;

		if (markup.kind == Markup::startTag)
		{
			const auto next = scanStartTag(xml, pos, fault);
			if (next == npos)
				return true;

			pos = next;
		}
		else if (markup.kind == Markup::endTag)
		{
			const auto gt = xml.find('>', pos + 2);
			if (gt == npos)
				return false;

			pos = gt + 1;
		}
		else
		{
			pos = markup.end;
		}
	}

	return false;
}

// -------------------------------------------------------- close-tag mismatch

struct CloseTagFault
{
	enum Kind { mismatched, neverClosed, unexpectedClose } kind = mismatched;
	size_t offset = 0;			// the tag to point the user at
	size_t openOffset = 0;		// where the element it should have closed was opened
	std::string closeName;
	std::string openName;
};

// tinyxml2 blames XML_ERROR_MISMATCHED_ELEMENT on the line the element was
// *opened* on, which in a long file is nowhere near the typo. Re-walk the tags
// keeping a stack of open elements, and find the closing tag that actually
// disagrees.
bool findCloseTagFault(std::string_view xml, CloseTagFault& out)
{
	std::vector<std::pair<std::string, size_t>> open;

	size_t pos = 0;
	while ((pos = xml.find('<', pos)) != npos)
	{
		const auto markup = classifyMarkup(xml, pos);

		if (markup.truncated)
			return false;

		if (markup.kind == Markup::startTag)
		{
			TagFault ignored;
			bool selfClosing = false;

			const auto next = scanStartTag(xml, pos, ignored, &selfClosing);
			if (next == npos)
				return false; // the tag itself is malformed; a different report covers it

			if (!selfClosing)
				open.emplace_back(ignored.elementName, pos);

			pos = next;
		}
		else if (markup.kind == Markup::endTag)
		{
			size_t nameEnd = pos + 2;
			while (nameEnd < xml.size() && XMLUtil::IsNameChar(static_cast<unsigned char>(xml[nameEnd])))
				++nameEnd;

			const auto name = xml.substr(pos + 2, nameEnd - (pos + 2));

			if (open.empty())
			{
				out = { CloseTagFault::unexpectedClose, pos, 0, std::string(name), {} };
				return true;
			}

			if (open.back().first != name)
			{
				out = { CloseTagFault::mismatched, pos, open.back().second, std::string(name), open.back().first };
				return true;
			}

			open.pop_back();

			const auto gt = xml.find('>', nameEnd);
			if (gt == npos)
				return false;

			pos = gt + 1;
		}
		else
		{
			pos = markup.end;
		}
	}

	// Ran out of document with elements still open. tinyxml2 reports this as a
	// bare XML_ERROR_PARSING against the INNERMOST unclosed element, so name that
	// one - both to agree with the line it blamed and because the forgotten
	// closing tag is nearly always the innermost.
	if (!open.empty())
	{
		out = { CloseTagFault::neverClosed, open.back().second, open.back().second, {}, open.back().first };
		return true;
	}

	return false;
}

// -------------------------------------------------------- plain-English text

const char* plainDescription(XMLError error)
{
	switch (error)
	{
	case XML_ERROR_EMPTY_DOCUMENT:				return "There is no XML here at all.";
	case XML_ERROR_FILE_NOT_FOUND:				return "The XML file could not be found.";
	case XML_ERROR_FILE_COULD_NOT_BE_OPENED:	return "The XML file could not be opened.";
	case XML_ERROR_FILE_READ_ERROR:				return "The XML file could not be read.";
	case XML_ERROR_PARSING_ELEMENT:				return "Malformed tag.";
	case XML_ERROR_PARSING_ATTRIBUTE:			return "Malformed attribute.";
	case XML_ERROR_PARSING_TEXT:				return "Malformed text.";
	case XML_ERROR_PARSING_CDATA:				return "Unterminated CDATA section (no closing \"]]>\").";
	case XML_ERROR_PARSING_COMMENT:				return "Unterminated comment (no closing \"-->\").";
	case XML_ERROR_PARSING_DECLARATION:			return "Malformed XML declaration (the \"<?xml ... ?>\" line).";
	case XML_ERROR_PARSING_UNKNOWN:				return "Unterminated \"<! ... >\" declaration.";
	case XML_ERROR_MISMATCHED_ELEMENT:			return "A closing tag does not match the tag it is closing.";
	case XML_ERROR_PARSING:						return "The XML could not be parsed.";
	default:									return "The XML could not be parsed.";
	}
}

// tinyxml2 tacks the enclosing element onto its long-form message as
// ": XMLElement name=Foo". Recover it - it is the only context it gives us.
std::string elementNameFromErrorStr(const XMLDocument& doc)
{
	const std::string_view errorStr = doc.ErrorStr() ? doc.ErrorStr() : "";
	constexpr std::string_view marker = "XMLElement name=";

	const auto at = errorStr.find(marker);
	if (at == npos)
		return {};

	return std::string(errorStr.substr(at + marker.size()));
}

const char* describeSourceKind(XmlSourceKind kind)
{
	switch (kind)
	{
	case XmlSourceKind::moduleResource:	return "XML resource compiled into the module binary";
	case XmlSourceKind::moduleFactory:	return "XML returned by the module's own factory";
	case XmlSourceKind::xmlFile:		return "XML description file in the module bundle";
	case XmlSourceKind::synthEditItself:return "XML built in to SynthEdit";
	case XmlSourceKind::unknown:
	default:							return nullptr;
	}
}

// What to say about a fault our re-scan found, and the follow-up that usually
// turns out to be the actual mistake.
struct FaultText
{
	std::string problem;
	std::string cause;
};

FaultText describeFault(const TagFault& fault)
{
	const std::string element = fault.elementName.empty()
		? std::string("a tag")
		: "<" + fault.elementName + ">";

	const std::string attribute = "'" + fault.attributeName + "'";

	switch (fault.kind)
	{
	case TagFaultKind::missingEquals:
		return {
			"In " + element + ", attribute " + attribute + " is not followed by '='.",
			"Usually an earlier attribute value on this tag contains a literal quote"
			" character - write it as &quot; - or an attribute was given no value."
		};

	case TagFaultKind::unquotedValue:
		return {
			"In " + element + ", the value of attribute " + attribute + " is not in quotes.",
			"Every XML attribute value must be quoted, even numbers: name=\"1\", not name=1."
		};

	case TagFaultKind::unterminatedValue:
		return {
			"In " + element + ", the value of attribute " + attribute + " has no closing quote.",
			"Check for a missing \" at the end of the value, or a literal quote inside"
			" it that should be written as &quot;."
		};

	case TagFaultKind::duplicateAttribute:
		return {
			element + " has two attributes called " + attribute + ".",
			"An attribute may appear only once per tag. Delete or rename the repeat."
		};

	case TagFaultKind::unexpectedCharacter:
		return {
			"Unexpected character in " + element + " where an attribute name or the end"
			" of the tag was expected.", {}
		};

	case TagFaultKind::unexpectedEnd:
		return { "The XML ends in the middle of " + element + ".", {} };

	case TagFaultKind::none:
	default:
		return {};
	}
}

FaultText describeCloseTagFault(const CloseTagFault& fault, int closeLine, int openLine)
{
	switch (fault.kind)
	{
	case CloseTagFault::mismatched:
		return {
			"</" + fault.closeName + "> on line " + std::to_string(closeLine) + " closes <"
				+ fault.openName + ">, which was opened on line " + std::to_string(openLine) + ".",
			"Check the spelling of the closing tag - or look for a tag opened in between"
			" and never closed."
		};

	case CloseTagFault::neverClosed:
		return {
			"<" + fault.openName + ">, opened on line " + std::to_string(openLine)
				+ ", is never closed.",
			"Add </" + fault.openName + ">, or make it self-closing: <" + fault.openName + " ... />"
		};

	case CloseTagFault::unexpectedClose:
		return { "</" + fault.closeName + "> has no matching opening tag.", {} };

	default:
		return {};
	}
}

// ------------------------------------------------------------------ assembly

void appendExcerpt(std::ostringstream& out, std::string_view xml, int errorLine)
{
	const int lastLine = countLines(xml);
	const int first = (std::max)(1, errorLine - contextLines);
	const int last = (std::min)(lastLine, errorLine + contextLines);

	// Right-align the line numbers so the XML itself stays in one column.
	const auto width = std::to_string(last).size();

	for (int line = first; line <= last; ++line)
	{
		auto number = std::to_string(line);
		number.insert(0, width - number.size(), ' ');

		// '>' marks the offending line. A caret under the exact column would be
		// nicer, but this text also ends up in a proportional-font message box
		// where nothing lines up; a gutter mark survives either way.
		out << (line == errorLine ? "> " : "  ") << number << " | "
			<< forDisplay(lineAt(xml, line)) << "\n";
	}
}

std::string readFileAsText(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return {};

	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

std::wstring formatXmlParseError(
	  const XMLDocument& doc
	, std::string_view xmlSource
	, std::wstring_view sourceName
	, XmlSourceKind sourceKind
)
{
	std::ostringstream out;

	out << "Module XML Error\n\n";

	if (!sourceName.empty())
		out << "File:    " << WStringToUtf8(std::wstring(sourceName)) << "\n";

	if (const auto* kind = describeSourceKind(sourceKind))
		out << "Source:  " << kind << "\n";

	int errorLine = doc.ErrorLineNum();

	std::string problem = plainDescription(doc.ErrorID());
	std::string cause;

	// Name the element tinyxml2 was inside, when it bothered to record one.
	if (const auto element = elementNameFromErrorStr(doc); !element.empty())
		problem += " (in element <" + element + ">)";

	// Then try to do better: re-walk the tags ourselves and name the actual
	// token. Only for the two errors that ARE tag faults, and only when our
	// finding is consistent with the line tinyxml2 blamed - otherwise we found a
	// different problem and would be pointing the user somewhere else entirely.
	const bool isTagError =
		doc.ErrorID() == XML_ERROR_PARSING_ATTRIBUTE ||
		doc.ErrorID() == XML_ERROR_PARSING_ELEMENT;

	if (isTagError && !xmlSource.empty())
	{
		TagFault fault;
		if (findFirstTagFault(xmlSource, fault) && fault.kind != TagFaultKind::none)
		{
			const int faultLine = lineOfOffset(xmlSource, fault.offset);

			// XML_ERROR_PARSING_ATTRIBUTE carries the line the attribute began
			// on, so it must match exactly. XML_ERROR_PARSING_ELEMENT carries the
			// line the *tag* began on, and the offending token can be further
			// down a tag that spans lines - hence >= rather than ==.
			const bool consistent = errorLine <= 0 ||
				(doc.ErrorID() == XML_ERROR_PARSING_ATTRIBUTE ? faultLine == errorLine : faultLine >= errorLine);

			if (consistent)
			{
				auto text = describeFault(fault);
				if (!text.problem.empty())
				{
					problem = std::move(text.problem);
					cause = std::move(text.cause);
					errorLine = faultLine;
				}
			}
		}
	}

	// A mismatched or missing closing tag is blamed on the line the element was
	// OPENED on, which in a long file is nowhere near the typo. Find the closing
	// tag that actually disagrees and point at that instead.
	const bool isCloseTagError =
		doc.ErrorID() == XML_ERROR_MISMATCHED_ELEMENT ||
		doc.ErrorID() == XML_ERROR_PARSING;

	if (isCloseTagError && !xmlSource.empty())
	{
		CloseTagFault fault;
		if (findCloseTagFault(xmlSource, fault))
		{
			const int openLine = lineOfOffset(xmlSource, fault.openOffset);
			const int faultLine = lineOfOffset(xmlSource, fault.offset);

			// tinyxml2 named the element and the line it opened on; if our walk
			// agrees on both, we are describing the same fault it hit.
			const auto blamed = elementNameFromErrorStr(doc);
			const bool consistent = errorLine <= 0 ||
				(openLine == errorLine && (blamed.empty() || blamed == fault.openName));

			if (consistent)
			{
				auto text = describeCloseTagFault(fault, faultLine, openLine);
				if (!text.problem.empty())
				{
					problem = std::move(text.problem);
					cause = std::move(text.cause);
					errorLine = faultLine;
				}
			}
		}
	}

	out << "Problem: " << problem << "\n";

	if (!cause.empty())
		out << "Cause:   " << cause << "\n";

	if (errorLine > 0)
	{
		out << "Line:    " << errorLine << "\n";

		if (!xmlSource.empty())
		{
			out << "\n";
			appendExcerpt(out, xmlSource, errorLine);
		}
	}
	else if (!xmlSource.empty() && xmlSource.size() < 200)
	{
		// No line to point at (an empty or tiny document) - just show the lot.
		out << "\nXML:     " << forDisplay(xmlSource) << "\n";
	}

	out << "\ntinyxml2 error code: " << doc.ErrorName() << "\n";

	return Utf8ToWstring(out.str());
}

std::wstring formatXmlParseErrorFromFile(
	  const XMLDocument& doc
	, const std::filesystem::path& xmlFile
)
{
	// A file tinyxml2 could not open or read has nothing to excerpt, and reading
	// it again here would only produce a second, less informative failure.
	const bool readable =
		doc.ErrorID() != XML_ERROR_FILE_NOT_FOUND &&
		doc.ErrorID() != XML_ERROR_FILE_COULD_NOT_BE_OPENED &&
		doc.ErrorID() != XML_ERROR_FILE_READ_ERROR;

	const auto text = readable ? readFileAsText(xmlFile) : std::string{};

	return formatXmlParseError(doc, text, xmlFile.wstring(), XmlSourceKind::xmlFile);
}
