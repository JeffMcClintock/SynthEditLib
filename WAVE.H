// wave file header
// wave.h

#include <cstdint>

struct wave_file_header
{
	char chnk1_name[4];
	int32_t chnk1_size;
	char chnk2_name[4];
	char chnk3_name[4];
	int32_t chnk3_size;
	uint16_t wFormatTag;
	uint16_t nChannels;
	int32_t nSamplesPerSec;
	int32_t nAvgBytesPerSec;
	uint16_t nBlockAlign;
	uint16_t wBitsPerSample;
	char chnk4_name[4];
	int32_t chnk4_size;
};
