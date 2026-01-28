#ifndef WAVERECORDER2_H_INCLUDED
#define WAVERECORDER2_H_INCLUDED

#include "../se_sdk3/mp_sdk_audio.h"
#include <memory>
#include <vector>

#pragma pack(push,1)
struct wave_file_header2
{
	char chnk1_name[4];
	int32_t chnk1_size;
	char chnk2_name[4];
	char chnk3_name[4];
	int32_t chnk3_size;
	int16_t wFormatTag;
	int16_t nChannels;
	int32_t nSamplesPerSec;
	int32_t nAvgBytesPerSec;
	int16_t nBlockAlign;
	int16_t wBitsPerSample;
	char chnk4_name[4];
	int32_t chnk4_size;
};
#pragma pack(pop)

class WaveRecorder2 : public MpBase2
{
public:
	WaveRecorder2();
	~WaveRecorder2();
	void CloseFile();
	int32_t open() override;
	void subProcess(int sampleFrames);
	void subProcess16bit(int sampleFrames);
	void onSetPins(void) override;

private:
	StringInPin pinFileName;
	IntInPin pinFormat;
	FloatInPin pinTimeLimit;

	std::vector< std::unique_ptr<AudioInPin> > AudioIns;
	std::vector< float* > AudioInPtrs;
	std::vector< unsigned char > AudioBuffer;
	FILE* outputStream;
	int64_t sampleFrameCount{};

	wave_file_header2 waveHeader;
	int64_t maxFrames = -1;
};

#endif

