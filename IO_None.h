// IO_None.h: interface for the IO_None class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_IO_None_H__F83E4F42_7985_11D6_AF7D_000103170662__INCLUDED_)
#define AFX_IO_None_H__F83E4F42_7985_11D6_AF7D_000103170662__INCLUDED_


#pragma once

#include <chrono>
#include "IO_base.h"

class IO_None : public IO_base
{
public:
	void MakeDriverList();
	IO_None();
	virtual ~IO_None();
	bool Prepare(const std::wstring& p_unique_id, bool audio_in_needed, bool audio_out_needed);
	bool StartStream();
	void Stop();
	void AudioInput( int /*sampleframes*/, float** /*outputs*/ ) {}
	void AudioOutput( int /*sampleframes*/, float** /*outputs*/ ) {}
	void Render();
	int getOffsetMs();
	virtual int MaxInputChannels()
	{
		return 0;
	}
	virtual int getSampleRate();
	virtual int getBufferSize();
	//	void set_latency(int l ){ latency = l;};
	int getLatency_ms()
	{
		return latency;
	}

private:
	int n_channels;
	int latency;
	std::chrono::steady_clock::time_point m_start_clock;
	int m_offset;
	int64_t elapsed_sample_time = 0;
};

#endif // !defined(AFX_IO_None_H__F83E4F42_7985_11D6_AF7D_000103170662__INCLUDED_)
