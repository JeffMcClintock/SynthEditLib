#pragma once
#include <string>
#include <vector>
#include <assert.h>

enum class enum_entry_type {Normal, Separator, Break, SubMenu, SubMenuEnd};

struct enum_entry
{
	int index{};
	int value{};
	std::wstring text;

	enum_entry_type getType() const
	{
		// Special commands (sub-menus)?
		if(text.size() < 4)
		{
			return enum_entry_type::Normal;
		}

		for(int i = 1; i < 4; ++i)
		{
			if(text[0] != text[i])
			{
				return enum_entry_type::Normal;
			}
		}

		switch (text[0])
		{
		case L'-':
			return enum_entry_type::Separator;
			break;

		case L'|':
			return enum_entry_type::Break;
			break;

		case L'>':
			return enum_entry_type::SubMenu;
			break;

		case L'<':
			return enum_entry_type::SubMenuEnd;
			break;
		}

		return enum_entry_type::Normal;
	}
};

// TODO: make more like STL, defer extracting text unless CurrentItem() called. hold pointer-to string, not copy-of.
class it_enum_list
{
public:
	it_enum_list(const std::wstring &p_enum_list);
	enum_entry * CurrentItem(){	assert(!IsDone() );	return &m_current;}
	enum_entry * operator*(){	assert(!IsDone() );	return &m_current;}
	bool IsDone(){return m_current.index == -1;}
	void Next();
	it_enum_list &operator++(){Next();return *this;} //Prefix increment
	void First();
	int size();
	bool FindValue( int p_value );
	bool FindIndex( int p_index );
	static bool IsValidValue( const std::wstring  &p_enum_list, int p_value );
	static int ForceValidValue( const std::wstring  &p_enum_list, int p_value );
	bool IsRange(){return m_range_mode;};
	int RangeHi(){return range_hi;}
	int RangeLo(){return range_lo;}

private:
	int StringToInt(const std::wstring  &string, int p_base = 10);
	std::wstring  m_enum_list;
	enum_entry m_current;
	bool m_range_mode;
	int range_lo{}; // also used as current position within string
	int range_hi{};
};

// Alternate approach, just spit out a standard container which can be iterated in the usual ways.
struct enum_entry2
{
	int32_t index;
	int32_t id;
	std::string text;
};

inline std::vector<enum_entry2> it_enum_list2(const std::string_view enum_list)
{
	std::vector< enum_entry2> res;

	// TODO handle "range" (e.g. 0-127)
	if (enum_list.find("range") == 0) // e.g. "range 0,127"
	{
		auto p = enum_list.find(' ');
		if (p == std::string::npos)
			return res;

		auto p2 = enum_list.find(',');
		if (p2 == std::string::npos)
			return res;

		auto lo = static_cast<int32_t>(strtol(enum_list.data() + p + 1, nullptr, 10));
		auto hi = static_cast<int32_t>(strtol(enum_list.data() + p2 + 1, nullptr, 10));

		int index = 0;
		for (int32_t i = lo; i <= hi; ++i)
		{
			res.push_back({ index, i, std::to_string(i) });
			++index;
		}

		return res;
	}

	int32_t index = 0;
	int32_t id = 0;
	for (auto p = enum_list.data(); p < enum_list.data() + enum_list.size();)
	{
		auto p2 = (char*)memchr(p, ',', enum_list.data() + enum_list.size() - p);
		if (!p2)
			p2 = (char*)enum_list.data() + enum_list.size();

		auto len = p2 - p;

		// find '=' sign, extract integer after it.
		auto p3 = (char*)memchr(p, '=', p2 - p);
		if (p3)
		{
			char* endptr{};
			auto explicitId = static_cast<int32_t>(strtol(p3 + 1, &endptr, 10));

			// ignore '=' followed by non-numeric characters. Avoids weirdness when user puts '=' in patch name.
			if (endptr != p3 + 1)
			{
				len = p3 - p;
				id = explicitId;
			}
		}

		res.push_back({ index, id, std::string(p, len) });

		p = p2 + 1;
		++id;
		++index;
	}

	return res;
}
