#pragma once
#include <assert.h>

/*
#include "UUID_Util.h"
*/

namespace SE_UUID_Util
{
	typedef uint8_t TUID[16];

	struct GuidStruct
	{
		uint32_t Data1;
		uint16_t Data2;
		uint16_t Data3;
		uint8_t Data4[8];
	};

	inline void fromString8(const char* string, char* data, int32_t i1, int32_t i2)
	{
		for (int32_t i = i1; i < i2; i++)
		{
			char s[3];
			s[0] = *string++;
			s[1] = *string++;
			s[2] = 0;

			int32_t d = 0;
			sscanf(s, "%2x", &d);
			data[i] = (char)d;
		}
	}

	inline void fromRegistryString(const char* string, TUID& returnValue)
	{
		if (!string || !*string)
			assert(false);
		if (strlen(string) != 38)
			assert(false);

		// e.g. {c200e360-38c5-11ce-ae62-08002b2b79ef}

		char s[10];
		GuidStruct g;

		strncpy(s, string + 1, 8);
		s[8] = 0;
		sscanf(s, "%x", &g.Data1);
		strncpy(s, string + 10, 4);
		s[4] = 0;
		sscanf(s, "%hx", &g.Data2);
		strncpy(s, string + 15, 4);
		s[4] = 0;
		sscanf(s, "%hx", &g.Data3);
		memcpy(returnValue, &g, 8);

		fromString8(string + 20, (char*) returnValue, 8, 10);
		fromString8(string + 25, (char*) returnValue, 10, 16);
	}
}
