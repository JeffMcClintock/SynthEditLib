#pragma once
#if !defined(AFX_IO_BASE_H__F83E4F43_7985_11D6_AF7D_000103170662__INCLUDED_)
#define AFX_IO_BASE_H__F83E4F43_7985_11D6_AF7D_000103170662__INCLUDED_

#include <list>
#include <vector>
#include <string>

class IO_base;

class IO_output_info
{
public:
	IO_output_info(IO_base* p_driver, const std::wstring& p_name, const std::wstring& p_guid) : m_name(p_name),m_guid(p_guid),m_driver(p_driver) {}
	virtual std::wstring getDescription()
	{
		return m_name;
	}
	virtual std::wstring getUniqueID()
	{
		return m_guid;
	}
	virtual ~IO_output_info() {}
	IO_base* Driver()
	{
		return m_driver;
	}

private:
	std::wstring m_name;
	std::wstring m_guid;
	IO_base* m_driver;
};

typedef std::list<IO_output_info*> DRVLIST;

class IO_base;
class SeAudioMaster;

struct AudioDriverClient
{
	virtual void process(int sampleFrames, const float** inputs, float** outputs, int inChannelCount, int outChannelCount) = 0;
	virtual bool isProcessingComplete() = 0;
	virtual void DoAsyncRestart() = 0;
	virtual void DoImmediateRestart() = 0;
};

class IO_base
{
	friend class it_IO_output_info;

protected:
	int preferredSampleRate{-1};

public:
	IO_base();
	virtual ~IO_base();

	virtual bool Prepare( const std::wstring& /*p_unique_id*/, bool /*audio_in_needed*/, bool /*audio_out_needed*/ )
	{
		m_error_state = false;
		return true;
	}
	std::wstring GetLastError()
	{
		return m_last_error;
	}
	virtual void MakeDriverList() = 0;
	virtual int getSampleRate() = 0;
	virtual int getBufferSize() = 0;
	virtual int getLatency_ms() = 0;
	virtual void setLatency_ms(int /*p_latency*/) {}
	void setPreferredSampleRate(int sr) { preferredSampleRate = sr; }
	virtual void SetClient(AudioDriverClient* pclient)
	{
		client = pclient;
	}
	void SetIoManager(class UIoManager* iom)
	{
		m_io_manager = iom;
	}
	
	virtual void Render() = 0;

	void RenderBlock(int sampleFrames);

	bool RunsRealTime()
	{
		return m_realtime;
	}
	void SetRealTime(bool realtime)
	{
		m_realtime = realtime;
	}
		
	virtual void ShowControlPanel() {}
	virtual bool hasControlPanel()
	{
		return false;
	}

//	void InitialMusicTimeUpdate();
	void on_error()
	{
		m_error_state = true;
	}

protected:
	std::wstring m_last_error;
	bool m_driver_list_initialised = false; // lazy initialisation to reduce io_ASIO memory leaks
	int m_read_buffer_remaining = 0;
	UIoManager* m_io_manager = {};
	bool m_error_state = {};
	bool m_realtime = true; // regular driver (set false for offline renderer)

public:
	DRVLIST m_driver_list;
	std::vector< std::vector<float> > pluginInputBuffers;
	std::vector< std::vector<float> > pluginOutputBuffers;
	std::vector<float*>pluginInputBuffersPtr;
	std::vector<float*>pluginOutputBuffersPtr;

	void AllocPluginBuffers(int size, int numIns, int numOuts);

	AudioDriverClient* client = {};

	void AddDriverDescription(IO_output_info* p_info);
	virtual int MaxOutputChannels()
	{
		return 2;
	}
	virtual int MaxInputChannels()
	{
		return 0;
	}
	virtual void Stop();
	virtual void Cleanup() {}
	virtual bool StartStream();
	virtual int getOffsetMs();
	virtual void AudioOutput( int /*sampleframes*/, float** /*outputs*/ ) {}
	virtual void AudioInput( int /*sampleframes*/, float** /*outputs*/ ) {}
	void float32toInt16(float* p_from, void* p_to, int frames);
	void float32toInt24(float* p_from, void* p_to, int frames);
	void float32toInt32(float* p_from, void* p_to, int frames);
	void int16toFloat32(void* p_from, float* p_to, int frames);
	void int24toFloat32(void* p_from, float* p_to, int frames);
	void int32toFloat32(void* p_from, float* p_to, int frames);
};

#endif // !defined(AFX_IO_BASE_H__F83E4F43_7985_11D6_AF7D_000103170662__INCLUDED_)
