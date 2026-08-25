#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace tinyxml2 { class XMLDocument; }

// Where the XML came from. A module developer has to know *which artifact* to go
// and edit -- an external .xml, a resource compiled into the binary, or the string
// the module's own factory handed us -- and the path alone does not say.
enum class XmlSourceKind
{
	unknown,
	moduleResource,		// XML resource compiled into the .sem / .gmpi binary
	moduleFactory,		// string returned by the module's own factory
	xmlFile,			// separate .xml file (e.g. in a bundle's Resources folder)
	synthEditItself,	// XML built in to SynthEdit
};

// Turn a failed tinyxml2 parse into something the user can act on: what is wrong
// in plain English, which line, the offending line of XML with its neighbours,
// and -- where we can work it out -- the exact attribute at fault.
//
// tinyxml2 on its own reports only an enum name ("XML_ERROR_PARSING_ATTRIBUTE"),
// which says neither where nor what. It also folds two quite different mistakes
// (a malformed attribute and a duplicated one) into that single code, so the
// scanner here re-walks the tag to tell them apart.
//
// xmlSource: the text that was handed to Parse(). Pass {} when it is no longer
// available -- only the source excerpt is lost, the rest of the report stands.
std::wstring formatXmlParseError(
	  const tinyxml2::XMLDocument& doc
	, std::string_view xmlSource
	, std::wstring_view sourceName
	, XmlSourceKind sourceKind = XmlSourceKind::unknown
);

// Same, for a document parsed with LoadFile(): re-reads the file for the excerpt.
// The file itself is named as the source - it is the thing to go and edit, and
// any enclosing bundle is its own parent folder.
std::wstring formatXmlParseErrorFromFile(
	  const tinyxml2::XMLDocument& doc
	, const std::filesystem::path& xmlFile
);
