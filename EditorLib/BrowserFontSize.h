#pragma once
#include <string>

// Font size for the Module Browser and Properties Browser panes.
//
// ONE setting drives BOTH panes. They used to differ: a Properties Browser row is
// built from gmpi's widget builders, which derive the font's body height from the
// widget's height as height * 0.8 (so 12.8 at a 16px row), while the Module Browser
// draws its own text and left TextBoxStyle's default body height of 12 in place.
// Nothing chose that difference -- it fell out of the two panes being written
// against different parts of the toolkit -- so the module list now states the
// shared body height explicitly and the two match at every size.
//
// Everything scales off browserRowHeight(): change the preference and the row
// height, the font, the line spacing and the section headings all follow. The
// panes read these at Body()/Render() time, so a change needs a rebuild of the
// view tree, not just a repaint -- see CSynthEditAppBase::setBrowserFontSize().

namespace SynthEdit
{

enum class BrowserFontSize
{
	Default,
	Large,
	Larger
};

inline BrowserFontSize& browserFontSizeStorage()
{
	static BrowserFontSize size = BrowserFontSize::Default;
	return size;
}

inline BrowserFontSize getBrowserFontSize()
{
	return browserFontSizeStorage();
}

// Prefer CSynthEditAppBase::setBrowserFontSize(), which also persists the choice
// and rebuilds the open browsers. This only moves the global the panes read.
inline void setBrowserFontSize(BrowserFontSize size)
{
	browserFontSizeStorage() = size;
}

// Multiplier applied to every text metric in the two panes.
inline float browserFontScale()
{
	switch (browserFontSizeStorage())
	{
	case BrowserFontSize::Large:  return 1.25f;
	case BrowserFontSize::Larger: return 1.5f;
	default:                      return 1.0f;
	}
}

// Height of one Properties Browser row, and of one Module Browser list item.
// 16 at Default -- what the Properties Browser has always used.
inline float browserRowHeight()
{
	return 16.0f * browserFontScale();
}

// The font's body height (ascent + descent) both panes draw their text at, matching
// what gmpi's widget builders derive for a row of browserRowHeight().
inline float browserBodyHeight()
{
	return browserRowHeight() * 0.8f;
}

inline std::string browserFontSizeToString(BrowserFontSize size)
{
	switch (size)
	{
	case BrowserFontSize::Large:  return "Large";
	case BrowserFontSize::Larger: return "Larger";
	default:                      return "Default";
	}
}

inline BrowserFontSize browserFontSizeFromString(const std::string& str)
{
	if (str == "Large")  return BrowserFontSize::Large;
	if (str == "Larger") return BrowserFontSize::Larger;
	return BrowserFontSize::Default;
}

} // namespace SynthEdit
