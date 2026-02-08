#include "wav_file.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>
#include <assert.h>
#include <stdexcept>

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

constexpr int32_t kFmtChunkSize = 16;
constexpr int32_t kWaveHeaderSize = static_cast<int32_t>(sizeof(wave_file_header));
constexpr int32_t kRiffHeaderOverhead = kWaveHeaderSize - 8;

constexpr int32_t kPcm8BitOffset = 0x80;
constexpr auto kValidBitDepths = std::to_array<unsigned int>({8, 16, 24, 32});

constexpr std::string_view kRiffId = "RIFF";
constexpr std::string_view kWaveId = "WAVE";
constexpr std::string_view kFmtId  = "fmt ";
constexpr std::string_view kDataId = "data";
constexpr std::string_view kSmplId = "smpl";

[[nodiscard]] bool chunkIdEquals(std::span<const char, 4> id, std::string_view expected)
{
	return std::ranges::equal(id, expected);
}

[[nodiscard]] std::filesystem::path toPath(const std::string& filename)
{
	// In C++20, u8path is deprecated. Use char8_t iterator constructor instead.
	auto p = reinterpret_cast<const char8_t*>(filename.data());
	return std::filesystem::path(p, p + filename.size());
}


// ---------------------------------------------------------------------------
//	WavFile constructor from filename
// ---------------------------------------------------------------------------
//!	Initialize an instance of WavFile by importing sample data from
//!	the file having the specified filename or path.
//!
//!	\param filename is the name or path of an wav file
//
WavFile::WavFile( const std::string & filename, int maxChannels, int extraInterpolationSamples)
{
    readWavData( filename, maxChannels, extraInterpolationSamples);
}


// -- export --

// ---------------------------------------------------------------------------
//	write 
// ---------------------------------------------------------------------------
//!	Export the sample data represented by this WavFile to
//!	the file having the specified filename or path. Export
//!	signed integer samples of the specified size, in bits
//!	(8, 16, 24, or 32).
//!
//!	\param filename is the name or path of the wav file
//!	to be created or overwritten.
//!	\param bps is the number of bits per sample to store in the
//!	samples file (8, 16, 24, or 32).If unspeicified, 16 bits
//!	is assumed.
//
void WavFile::write( const std::string & filename, unsigned int bps )
{
	if (std::ranges::find(kValidBitDepths, bps) == kValidBitDepths.end())
	{
		throw std::runtime_error("Invalid bits-per-sample.");
	}

	{
		bool floatFormat = bps == 32; // false for 16-bit PCM
		int bits_per_sample = bps;
		int sample_rate = static_cast<int>(rate_);
		float* src = nullptr;

		const auto sample_count = samples_.size() / numchans_;
		src = samples_.data();

		wave_file_header wav_head{};
		std::ranges::copy(kRiffId, wav_head.chnk1_name);
		std::ranges::copy(kWaveId, wav_head.chnk2_name);
		std::ranges::copy(kFmtId, wav_head.chnk3_name);
		std::ranges::copy(kDataId, wav_head.chnk4_name);

		if (floatFormat)
		{
			wav_head.wFormatTag = kWaveFormatIeeeFloat;
			bits_per_sample = 32;
		}
		else
		{
			wav_head.wFormatTag = kWaveFormatPcm;
			bits_per_sample = bps;
		}

		wav_head.wBitsPerSample = static_cast<uint16_t>(bits_per_sample);
		wav_head.chnk3_size = kFmtChunkSize;
		wav_head.nChannels = numchans_;
		wav_head.chnk4_size = (int32_t)(sample_count * wav_head.wBitsPerSample / 8 * wav_head.nChannels);
		wav_head.chnk1_size = wav_head.chnk4_size + kRiffHeaderOverhead;
		wav_head.nSamplesPerSec = sample_rate;
		wav_head.nAvgBytesPerSec = wav_head.nSamplesPerSec * wav_head.nChannels * wav_head.wBitsPerSample / 8;
		wav_head.nBlockAlign = (wav_head.wBitsPerSample / 8) * wav_head.nChannels;

		std::ofstream myfile;
		myfile.open(toPath(filename), std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
		if (!myfile)
		{
			throw std::runtime_error("Can't save wave file.");
		}

		myfile.write(reinterpret_cast<char*>(&wav_head), kWaveHeaderSize);

		std::vector<float*> samples;
		for (int c = 0; c < wav_head.nChannels; ++c)
			samples.push_back(src + c * sample_count);

		if (floatFormat)
		{
			for (int i = 0; i < sample_count; ++i)
			{
				for (int c = 0; c < wav_head.nChannels; ++c)
				{
				float s = static_cast<float>(*samples[c]);

				myfile.write(reinterpret_cast<char*>(&s), sizeof(float));
					samples[c]++;
				}
			}
		}
		else
		{
			int bytesPerSample = bps / 8;
			double multiplier = static_cast<double>(1u << (bps - 1));
			int sample_int;
			char* data = reinterpret_cast<char*>(&sample_int);
			int add = bps == 8 ? 128 : 0;
			for (int i = 0; i < sample_count; ++i)
			{
				for (int c = 0; c < wav_head.nChannels; ++c)
				{
					double s = *samples[c];
					sample_int = add + static_cast<int>(0.5 + s * multiplier);

					myfile.write(data, bytesPerSample);
					samples[c]++;
				}
			}
		}

		myfile.close();
	}

}

// -- access --

// ---------------------------------------------------------------------------
//	numChannels 
// ---------------------------------------------------------------------------
//!	Return the number of channels of audio samples represented by
//! this WavFile, 1 for mono, 2 for stereo.
//
unsigned int 
WavFile::numChannels( void ) const
{
    return numchans_;
}

// ---------------------------------------------------------------------------
//	numFrames 
// ---------------------------------------------------------------------------
//!	Return the number of sample frames represented in this WavFile.
//!	A sample frame contains one sample per channel for a single sample
//!	interval (e.g. mono and stereo samples files having a sample rate of
//!	44100 Hz both have 44100 sample frames per second of audio samples).
//
 WavFile::size_type  
 WavFile::numFrames( void ) const
 {
 	return numFrames_;
 }

// ---------------------------------------------------------------------------
//	sampleRate 
// ---------------------------------------------------------------------------
//!	Return the sampling freqency in Hz for the sample data in this
//!	WavFile.
//
double  
WavFile::sampleRate( void ) const
{
	return rate_;
}

// ---------------------------------------------------------------------------
//	samples 
// ---------------------------------------------------------------------------
//!	Return a reference (or const reference) to the vector containing
//!	the floating-point sample data for this WavFile.
//
WavFile::samples_type & 
WavFile::samples( void )
{
	return samples_;
}

//!	Return a const reference (or const reference) to the vector containing
//!	the floating-point sample data for this WavFile.
//
const WavFile::samples_type & 
WavFile::samples( void ) const
{
	return samples_;
}


// ---------------------------------------------------------------------------
//	readAiffData
// ---------------------------------------------------------------------------
//	Import data from an AIFF file on disk.
//
void WavFile::readWavData( const std::string & filename, int maxChannels, int extraInterpolationSamples )
{
	std::ifstream myfile;
	myfile.open(toPath(filename), std::ios_base::in | std::ios_base::binary);
	if (!myfile)
	{
		return;
	}

    MYWAVEFORMATEX waveheader{};
	std::vector<char> waveData;
	std::size_t waveDataBytes{};

	int chunkLength;
	char chunkName[4];
	myfile.read(chunkName, 4);

	if (!chunkIdEquals(chunkName, kRiffId))
	{
		throw std::runtime_error("Input stream doesn't comply with the RIFF specification");
		return;
	}

	myfile.read(reinterpret_cast<char*>(&chunkLength), 4);

	// WAVE chunk.
	myfile.read(chunkName, 4);
	if (!chunkIdEquals(chunkName, kWaveId))
	{
		throw std::runtime_error("Input stream doesn't comply with the WAVE specification");
		return;
	}

	while (!myfile.eof())
	{
		chunkName[0] = 0;
		chunkLength = 0;
		myfile.read(chunkName, 4);
		myfile.read(reinterpret_cast<char*>(&chunkLength), 4);

		if (chunkLength < 0)
		{
			throw std::runtime_error("Corrupt WAVE chunk length");
		}

		if (chunkIdEquals(chunkName, kFmtId))
		{
			myfile.read(reinterpret_cast<char*>(&waveheader), (std::min)(static_cast<size_t>(chunkLength), sizeof(waveheader)));
			if (waveheader.wBitsPerSample == 0)
			{
				throw std::runtime_error("The input stream uses an unhandled SignificantBitsPerSample parameter");
				return;
			}
			if (chunkLength > sizeof(waveheader))
			{
				myfile.ignore(static_cast<std::streamsize>(chunkLength - sizeof(waveheader)));
			}
		}
		else
		{
			if (chunkIdEquals(chunkName, kDataId))
			{
				waveDataBytes = static_cast<std::size_t>(chunkLength);
				waveData.resize(waveDataBytes);
				myfile.read(waveData.data(), static_cast<std::streamsize>(waveDataBytes));
			}
			else
			{
				// Next chunk.
				myfile.ignore(static_cast<std::streamsize>(chunkLength));
			}
		}
	}

	if (waveheader.nBlockAlign == 0 || waveDataBytes == 0)
	{
		samples_.clear();
		numFrames_ = 0;
		numchans_ = 0;
		return;
	}

	rate_ = waveheader.nSamplesPerSec;
	numFrames_ = static_cast<int>(waveDataBytes / waveheader.nBlockAlign);
	numchans_ = std::min(maxChannels, (int)waveheader.nChannels);

	int totalSamples = numchans_ * (numFrames_ + extraInterpolationSamples * 2);

	samples_.assign(totalSamples, 0.0);

	float* dest = samples_.data();
	const auto waveDataView = std::span<const std::byte>(reinterpret_cast<const std::byte*>(waveData.data()), waveDataBytes);
	{
		for (unsigned int channel = 0; channel < numchans_; ++channel)
		{
			for (int i = 0; i < extraInterpolationSamples; ++i)
			{
				*dest++ = 0.0f;
			}

			switch (waveheader.wFormatTag)
			{
			case kWaveFormatPcm:
			{
				switch (waveheader.wBitsPerSample)
				{
				case 8:
				{

					constexpr float toFloatMultiplier = 1.f / (1 << 7);

					auto i8 = reinterpret_cast<const unsigned char*>(waveDataView.data()) + channel;
					for (int i = 0; i < numFrames_; ++i)
					{
						int32_t s = static_cast<int32_t>(*i8) - kPcm8BitOffset;
						*dest++ = toFloatMultiplier * s;
						i8 += waveheader.nChannels;
					}
				}
				break;

				case 16:
				{
					constexpr float toFloatMultiplier = 1.f / (1 << 15);

					auto i16 = reinterpret_cast<const int16_t*>(waveDataView.data()) + channel;
					for (int i = 0; i < numFrames_; ++i)
					{
						*dest++ = toFloatMultiplier * *i16;
						i16 += waveheader.nChannels;
					}
				}
				break;

				case 24:
				{
					constexpr int sampleBytes = 3;

					constexpr float toFloatMultiplier = 1.f / (1 << 31);

					auto i24 = reinterpret_cast<const unsigned char*>(waveDataView.data()) + sampleBytes * channel;
					for (int i = 0; i < numFrames_; ++i)
					{
						const int32_t t = (i24[0] << 8) + (i24[1] << 16) + (i24[2] << 24);
						*dest++ = toFloatMultiplier * t;
						i24 += sampleBytes * waveheader.nChannels;
					}
				}
				break;

				case 32:
				{
					constexpr float toFloatMultiplier = 1.f / (1 << 31);

					auto i32 = reinterpret_cast<const int32_t*>(waveDataView.data()) + channel;
					for (int i = 0; i < numFrames_; ++i)
					{
						*dest++ = toFloatMultiplier * *i32;
						i32 += waveheader.nChannels;
					}
				}
				break;

				default:
				{
				}

				}
			}

			break;

		case kWaveFormatIeeeFloat:
				if (waveheader.wBitsPerSample == 32)
				{
					auto f32 = reinterpret_cast<const float*>(waveDataView.data()) + channel;
					for (int i = 0; i < numFrames_; ++i)
					{
						*dest++ = *f32;
						f32 += waveheader.nChannels;
					}
				}
				break;

			default:
				;
			};
		}
	}

}

std::unique_ptr<WavFileCursor> WavFileStreaming::open(const std::string& pfilename)
{
	filename = pfilename;

	wavedata_offset = std::streampos(-1);
	totalSampleFrames = 0;
	waveheader = {};
	m_sampler_data = {};

	std::ifstream myfile;
	myfile.open(toPath(filename), std::ios_base::in | std::ios_base::binary);
	if (!myfile)
	{
		return nullptr;
	}

	unsigned int chunkLength;
	char chunkName[4];
	myfile.read(chunkName, 4);

	if (!chunkIdEquals(chunkName, kRiffId))
	{
		throw std::runtime_error("Input stream doesn't comply with the RIFF specification");
	}

	myfile.read(reinterpret_cast<char*>(&chunkLength), 4);

	// WAVE chunk.
	myfile.read(chunkName, 4);
	if (!chunkIdEquals(chunkName, kWaveId))
	{
		throw std::runtime_error("Input stream doesn't comply with the WAVE specification");
	}

	while (!myfile.eof())
	{
		chunkName[0] = 0;
		chunkLength = 0;
		myfile.read(chunkName, 4);
		myfile.read(reinterpret_cast<char*>(&chunkLength), 4);

		if (chunkIdEquals(chunkName, kFmtId))
		{
			const size_t expectedSize = sizeof(waveheader);
			myfile.read(reinterpret_cast<char*>(&waveheader), (std::min)(static_cast<size_t>(chunkLength), expectedSize));
			if (waveheader.wBitsPerSample == 0)
			{
				throw std::runtime_error("The input stream uses an unhandled SignificantBitsPerSample parameter");
			}

			// Float wave sample should be 32 bit, however cooledit 96 saves
			// waves as 16 bit INTEGERS (and uses headertag FLOAT, confused?)
			if (waveheader.wFormatTag == kWaveFormatIeeeFloat && waveheader.wBitsPerSample < 32)
			{
				waveheader.wFormatTag = kWaveFormatPcm;
			}

			if (chunkLength > expectedSize)
			{
				myfile.ignore(static_cast<std::streamsize>(chunkLength - expectedSize));
			}
		}
		else
		{
			if (chunkIdEquals(chunkName, kDataId))
			{
				totalSampleFrames = chunkLength / waveheader.nBlockAlign;

				// note fileposition of first sample.
				wavedata_offset = myfile.tellg();

				myfile.ignore(static_cast<std::streamsize>(chunkLength));
			}
			else
			{
				if (chunkIdEquals(chunkName, kSmplId))
				{
					const size_t expectedSize = sizeof(m_sampler_data);
					myfile.read(reinterpret_cast<char*>(&m_sampler_data), (std::min)(static_cast<size_t>(chunkLength), expectedSize));
					if (chunkLength > expectedSize)
					{
						myfile.ignore(static_cast<std::streamsize>(chunkLength - expectedSize));
					}

					// cope with corrupt files
					if (m_sampler_data.Loops[0].dwStart < 0 || m_sampler_data.Loops[0].dwStart >= totalSampleFrames || m_sampler_data.Loops[0].dwEnd < 0 || m_sampler_data.Loops[0].dwEnd >= totalSampleFrames)
					{
						m_sampler_data.cSampleLoops = 0;
					}
				}
				else
				{
					// Next chunk.
					myfile.ignore(static_cast<std::streamsize>(chunkLength));
				}
			}
		}
	}

	switch (waveheader.wFormatTag)
	{
	case kWaveFormatPcm:
		if (waveheader.wBitsPerSample != 8 && waveheader.wBitsPerSample != 16 && waveheader.wBitsPerSample != 24 && waveheader.wBitsPerSample != 32)
		{
			throw std::runtime_error("This WAVE file format is not supported.  Convert it to 16 bit uncompressed mono or stereo.");
		}

		break;

	case kWaveFormatIeeeFloat:
		if (waveheader.wBitsPerSample != 32 ) //&& waveheader.wBitsPerSample != 64)
		{
			throw std::runtime_error("This WAVE file format is not supported.  Convert it to 16 bit uncompressed mono or stereo.");
		}
		break;

	default:
		throw std::runtime_error("This WAVE file format is not supported.  Convert it to PCM or Float");
	};

	if (totalSampleFrames == 0 || wavedata_offset == std::streampos(-1))
		return nullptr;

	return std::make_unique<WavFileCursor>(this);
}

void WavFileCursor::DiskSamplesToBuffer(MYWAVEFORMATEX& waveheader, int sampleReadCount, float* dest)
{
	{
		const auto readBytes = static_cast<std::streamsize>(sampleReadCount) * waveheader.wBitsPerSample / 8;

		// TODO for float, just read direct into buffer, for int32, read indirect, then convert in-place.
		myfile.clear();
		myfile.read(conversionBuffer.data(), readBytes);
		assert(!myfile.fail());
	}

	const auto source = conversionBuffer.data();
	int c = sampleReadCount;

	// convert samples
	switch (waveheader.wFormatTag)
	{
	case kWaveFormatPcm:
		{
			switch (waveheader.wBitsPerSample)
			{
			case 8:
			{
				constexpr float toFloatMultiplier = 1.f / (1 << 7);

			auto i8 = reinterpret_cast<const unsigned char*>(source);
				while (c-- > 0)
				{
				int32_t s = static_cast<int32_t>(*i8++) - 0x80;
					*dest++ = toFloatMultiplier * s;
				}
			}
			break;

			case 16:
			{
				constexpr float toFloatMultiplier = 1.f / (1 << 15);

			auto i16 = reinterpret_cast<const int16_t*>(source);
				while (c-- > 0)
				{
					*dest++ = toFloatMultiplier * *i16++;
				}
			}
			break;

			case 24:
			{
				constexpr float toFloatMultiplier = 1.f / (1 << 31);

			auto i24 = reinterpret_cast<const unsigned char*>(source);
				while (c-- > 0)
				{
					const int32_t t = (i24[0] << 8) + (i24[1] << 16) + (i24[2] << 24);
				*dest++ = toFloatMultiplier * t;
				i24 += 3;
				}
			}
			break;

			case 32:
			{
				constexpr float toFloatMultiplier = 1.f / (1 << 31);

			auto i32 = reinterpret_cast<const int32_t*>(source);
				while (c-- > 0)
				{
					*dest++ = toFloatMultiplier * *i32++;
				}
			}
			break;

			}
		}
		break;

		case kWaveFormatIeeeFloat:
		{
		auto f32 = reinterpret_cast<const float*>(source);
			while (c-- > 0)
			{
				*dest++ = *f32++;
			}
		}
		break;
	}
}

// !! buggy with looping, results in a few zero samples at loop point. ref 'Sine Looped (hard)_8bit_mono.wav'
std::tuple<const float*, int> WavFileCursor::GetMoreSamples(bool gate)
{
	// copy overlap from last buffer.
	const int overlapSamples = SampleBufferOverlap * ChannelsCount();
	const int overlapSamplesTotal = overlapSamples * 2;

	// Copy end of last buffer to start of this one.
	for (int i = 0 ; i < overlapSamplesTotal; ++i )
		buffer[i] = buffer[lastSentSampleIndex + i];

#ifdef _DEBUG
	for (size_t i = overlapSamplesTotal; i < buffer.size(); ++i)
		buffer[i] = 1000000.f;
#endif

	int bufferWriteStart;
	int64_t filePosition;

	if (samplePosition == loopEndMarker && gate) // reached loop point?
	{
		// Rewind file to loop start.
		samplePosition = sampleData->m_sampler_data.Loops[0].dwStart * ChannelsCount();
		int64_t advanceBytes = sampleData->waveheader.nBlockAlign * (samplePosition / ChannelsCount());
		myfile.seekg(sampleData->wavedata_offset);
		myfile.seekg(advanceBytes, std::ios_base::cur);

		// Keep half of the buffered samples from before the loop end,
		// but write over right-hand side of loop point with samples from start of loop.
		bufferWriteStart = overlapSamples;
		filePosition = samplePosition;
	}
	else
	{
		bufferWriteStart = overlapSamplesTotal;
		filePosition = samplePosition + overlapSamples;
	}

	int64_t sampleReadCount;
	if (samplePosition < loopEndMarker)
	{
		sampleReadCount = loopEndMarker - samplePosition; // read 4-samples past loop-end. Might fail to loop if file not that long.
	}
	else
	{
		sampleReadCount = sampleData->totalSamples() - samplePosition; // not  - filePosition;
	}
	assert(sampleReadCount >= 0);

	// Limit to available buffer size.
	int64_t spaceInBuffer = static_cast<int>(buffer.size()) - bufferWriteStart;
	sampleReadCount = (std::min)(sampleReadCount, spaceInBuffer);
	int64_t returnSamplesCount = sampleReadCount - (overlapSamplesTotal - bufferWriteStart);

	// have we gone "off end" of sample? pad with zeros.
    const int64_t remainingDiskSamples = sampleData->totalSamples() - filePosition;
    const int64_t zeroPadding = (std::max)((int64_t)0, sampleReadCount - remainingDiskSamples);
 
    sampleReadCount -= zeroPadding;  // 0 -> 4 frames.

    for (int i = 0; i < zeroPadding; ++i)
        buffer[bufferWriteStart + sampleReadCount + i] = 0.f;
    
    if(sampleReadCount > 0)
    {
        // Read samples off disk.
        auto dest = buffer.data() + bufferWriteStart;
        DiskSamplesToBuffer(sampleData->waveheader, static_cast<int>(sampleReadCount), dest);
    }
	auto returnData = buffer.data() + overlapSamples;
	int returnSamples = static_cast<int>(returnSamplesCount);
	lastSentSampleIndex = returnSamplesCount;

	// For very first buffer, the overlap copied from end of previous buffer is not useful, so adjust return range to suit.
	// Skip first 4 zero samples at very start.
	if (samplePosition == 0)
	{
		int unused = SampleBufferOverlap * ChannelsCount();
		returnSamples -= unused;
		returnData += unused;
	}

	samplePosition += returnSamples;

	return std::tuple<float*, int>(returnData, returnSamples);
}

WavFileCursor::WavFileCursor(WavFileStreaming* pfile) : sampleData(pfile)
{
	const int bufferSize = 4096;
	buffer.resize(bufferSize);

	auto wave_data_bytes = bufferSize * sampleData->waveheader.wBitsPerSample / 8;
	conversionBuffer.resize(wave_data_bytes);

	if (sampleData->m_sampler_data.cSampleLoops > 0)
	{
	loopEndMarker = sampleData->m_sampler_data.Loops[0].dwEnd * ChannelsCount();
	}

	myfile.open(toPath(sampleData->filename), std::ios_base::in | std::ios_base::binary);
}

void WavFileCursor::Reset()
{
	// put some zeros in buffer 'before' sample start.
	lastSentSampleIndex = 0;
	for (auto i = 0; i < SampleBufferOverlap * ChannelsCount() * 2; ++i)
		buffer[i] = 0.0f;

	myfile.seekg(sampleData->wavedata_offset);
	samplePosition = 0;
}
