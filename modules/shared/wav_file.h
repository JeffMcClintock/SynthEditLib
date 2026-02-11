#pragma once
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#pragma pack(push,1)

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

struct MYWAVEFORMATEX
{
	uint16_t  wFormatTag;         /* format type */
	uint16_t  nChannels;          /* number of channels (i.e. mono, stereo...) */
	int32_t	  nSamplesPerSec;     /* sample rate */
	int32_t   nAvgBytesPerSec;    /* for buffer estimation */
	uint16_t  nBlockAlign;        /* block size of data */
	uint16_t  wBitsPerSample;     /* number of bits per sample of mono data */
	uint16_t  cbSize;             /* the count in bytes of the size of */
								  /* extra information (after cbSize) */
};

// http://www.borg.com/~jglatt/tech/wave.htm
struct SampleLoop
{
	int32_t  dwIdentifier;
	int32_t  dwType;
	uint32_t  dwStart; // byte offset
	uint32_t  dwEnd;
	int32_t  dwFraction;
	int32_t  dwPlayCount; //value of 0 specifies an infinite sustain loop
};

struct SamplerChunk
{
	//uint16_t      chunkID;
	//int32_t       chunkSize;

	int32_t       dwManufacturer;
	int32_t       dwProduct;
	int32_t       dwSamplePeriod;
	int32_t       dwMIDIUnityNote;
	int32_t       dwMIDIPitchFraction;
	int32_t       dwSMPTEFormat;
	int32_t       dwSMPTEOffset;
	int32_t       cSampleLoops;
	int32_t       cbSamplerData;
	struct SampleLoop Loops[8]; // actual number is variable. We will read 8 maximum.
};
#pragma pack(pop)

inline constexpr uint16_t kWaveFormatPcm = 1;
inline constexpr uint16_t kWaveFormatIeeeFloat = 0x0003;

class WavFile
{
//  -- public interface --
public:

//  -- types --

    //! The type of the sample storage in an WavFile.
    using samples_type = std::vector<float>;
    
    //! The type of all size parameters for WavFile.
    using size_type = samples_type::size_type;
    
    //! Initialize an instance of WavFile by importing sample data from
    //! the file having the specified filename or path.
    //!
    //! \param filename is the name or path of an AIFF samples file
    explicit WavFile( const std::string & filename, int maxChannels = 1, int extraInterpolationSamples = 0);

    //! Initialize an instance of WavFile with samples rendered
    //! from a sequnence of Partials. The Partials in the
    //! specified half-open (STL-style) range are rendered at 
    //! the specified sample rate, using the (optionally) 
    //! specified Partial fade time (see Synthesizer.h
    //! for an examplanation of fade time). Other synthesis 
	//!	parameters are taken from the Synthesizer DefaultParameters.
	//!	
	//!	\sa Synthesizer::DefaultParameters
    //!
    //! \param begin_partials is the beginning of a sequence of Partials
    //! \param end_partials is (one-past) the end of a sequence of
    //! Partials
    //! \param samplerate is the rate (Hz) at which Partials are rendered
    //! \param fadeTime is the Partial fade time (seconds) for rendering
    //! the Partials on the specified range. If unspecified, the
    //! fade time is taken from the Synthesizer DefaultParameters.
    //!
    //! If compiled with NO_TEMPLATE_MEMBERS defined, this member accepts
    //! only PartialList::const_iterator arguments.

    //! Return the number of channels of audio samples represented by
    //! this WavFile, 1 for mono, 2 for stereo.
    [[nodiscard]] unsigned int numChannels( void ) const;

    //! Return the number of sample frames represented in this WavFile.
    //! A sample frame contains one sample per channel for a single sample
    //! interval (e.g. mono and stereo samples files having a sample rate of
    //! 44100 Hz both have 44100 sample frames per second of audio samples).
    [[nodiscard]] size_type numFrames( void ) const;

    //! Return the sampling freqency in Hz for the sample data in this
    //! WavFile.
    [[nodiscard]] double sampleRate( void ) const;
    
    //! Return a reference (or const reference) to the vector containing
    //! the floating-point sample data for this WavFile.
    samples_type & samples( void );

    //! Return a const reference (or const reference) to the vector containing
    //! the floating-point sample data for this WavFile.
    [[nodiscard]] const samples_type & samples( void ) const;

//  -- export --

    //! Export the sample data represented by this WavFile to
    //! the file having the specified filename or path. Export
    //! signed integer samples of the specified size, in bits
    //! (8, 16, 24, or 32).
    //!
    //! \param filename is the name or path of the AIFF samples file
    //! to be created or overwritten.
    //! \param bps is the number of bits per sample to store in the
    //! samples file (8, 16, 24, or 32).If unspeicified, 16 bits
    void write( const std::string & filename, unsigned int bps = 16 );

private:
    double rate_ = 1.0;     // sample rate
    unsigned int numchans_ = 1;
	int numFrames_ = 0;
    samples_type samples_;      // floating point samples [-1.0, 1.0]

	//	Import data from an file on disk.
    void readWavData( const std::string & filename, int maxChannels = 1, int extraInterpolationSamples = 0);
};

struct WavFileCursor
{
	static constexpr int SampleBufferOverlap = 4;

	std::ifstream myfile;
	class WavFileStreaming* sampleData;
	[[nodiscard]] int SampleRate() const;
	[[nodiscard]] int ChannelsCount() const;
	std::vector<float> buffer;
	std::vector<char> conversionBuffer;
	int64_t samplePosition = 0;
	int64_t lastSentSampleIndex = 0;
	int loopEndMarker = -1;

	WavFileCursor(class WavFileStreaming* pfile);

	void DiskSamplesToBuffer(MYWAVEFORMATEX & waveheader, int sampleReadCount, float* dest);

	[[nodiscard]] std::tuple<const float*, int> GetMoreSamples(bool gate);

	void Reset();
};

class WavFileStreaming
{
	friend struct WavFileCursor;

	MYWAVEFORMATEX waveheader;
	std::streampos wavedata_offset;
	std::string filename;
	unsigned int totalSampleFrames; // i.e. per channel.
	SamplerChunk m_sampler_data;

public:
	[[nodiscard]] unsigned int totalSamples() const
	{
		return totalSampleFrames * waveheader.nChannels;
	}

	std::unique_ptr<WavFileCursor> open(const std::string& filename);
};

inline int WavFileCursor::SampleRate() const
{
	return sampleData->waveheader.nSamplesPerSec;
}

inline int WavFileCursor::ChannelsCount() const
{
	return sampleData->waveheader.nChannels;
}

