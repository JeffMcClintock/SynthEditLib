#include <sstream>
#include <chrono>
#include "UIoManager.h"
#include "SeAudioMaster.h" // needed for UI msgs? best place for it?????
#include "ug_soundcard_in.h"
#include "ug_soundcard_out.h"
#include "ug_midi_out.h"
#include "IO_base.h"
#include "midi_defs.h"
#include "modules_internal/MidiIn.h"
#include "SynthRuntime_editor.h"
#include "conversion.h"

#define IRP_MJ_CREATE 0x00

// prevent repeated bugging of user
int UIoManager::m_inhibit_midi_in_warning = 0;


UIoManager::UIoManager() :
	realtime_flag(false)
	,m_audio_driver_prepared(false)
{
}

bool UIoManager::Initialise(class SynthRuntime_editor* pclient, IO_base* p_audio_driver, const std::wstring& m_driver_guid, bool in_required, bool out_required )
{
	m_audio_driver = p_audio_driver;
	m_audio_driver_guid = m_driver_guid;

	if( m_audio_driver == 0 )
	{
		//		std::wstring msg;
		std::wstring driver_name(m_audio_driver_guid);

		if( driver_name.empty() ) //why if not used???
		{
			driver_name = L"Default DirectSound Driver";
		}

		//		msg.Format(L"Audio Driver not Available : %s", driver_name );
		std::wostringstream oss;
		oss << L"Audio Driver not Available : " << driver_name;
		ErrorMessage( oss.str() );
		on_error();
		return false;
	}

	m_audio_driver->SetClient(pclient);
	m_audio_driver->SetIoManager(this);

	if( ! m_audio_driver->Prepare( m_audio_driver_guid, in_required, out_required ) )
	{
		ErrorMessage( m_audio_driver->GetLastError() );
		on_error();
		return false;
	}

	m_audio_driver_prepared = true;

	return true;
}

void UIoManager::OnRebuildDsp()
{
	hasAudioOutput = hasAudioInput = false;
	MidiInUg = {};
}

UIoManager::~UIoManager()
{
	if( m_audio_driver ) // device may hve been removed since preferences set.
	{
		// handle abnormal termination, where driver prepared, but never started
		// e.g. "feedback path detected error"
		if( m_audio_driver_prepared )
		{
			m_audio_driver->Stop();
		}

		m_audio_driver->Cleanup();
	}
}

bool UIoManager::StartSoundcard()
{
	time_offset_audio = 0;

	// init
	int num_ins = 0;
	int num_outs = 0;

	if(hasAudioOutput)  // Directsound needed?
	{
		num_outs = 2;
	}

	// Wave input init
	if(hasAudioInput)
	{
		num_ins = 2;
	}

	if(m_audio_driver)
		latency_ms = m_audio_driver->getLatency_ms();
	else
		latency_ms = 0;

	// _RPT1(_CRT_WARN, "Latency %d ms\n", latency_ms );

	// no need to start driver if no audio in/out module
	if( num_ins == 0 && num_outs == 0 )
		return true;

	return true;
}

void UIoManager::RunAt( timestamp_t p_sampleclock, io_func p_func )
{
#ifdef _DEBUG
	const auto driverBlockSize = m_audio_driver->getBufferSize();
	assert(p_sampleclock % driverBlockSize == 0); // don't subdivide audio buffers from the audio driver.
#endif

	// only copies some of function pointer, assumes remainder is zeroed.
	int* i = (int*) &p_func;
	
	AddEvent( new_SynthEditEvent( p_sampleclock, UET_IO_FUNC , 0, i[0], i[1], 0 ));
}

void UIoManager::RenderBlock(int sampleframes, const float** inputs, float** outputs, size_t inChannelCount, size_t outChannelCount)
{
	assert(sampleframes > 0);
	timestamp_t current_sample_clock = SampleClock();

#ifdef _DEBUG
	timestamp_t end_time = current_sample_clock + sampleframes;
#endif
	
	while (!events.empty())
	{
		auto e2 = events.front();

		assert(e2->timeStamp == current_sample_clock || e2->timeStamp >= end_time); // events not to subdivide audio driver blocksize thanks.

		if (e2->timeStamp != current_sample_clock)
		{
			break;
		}

		events.pop_front();
		HandleEvent(e2);

		delete_SynthEditEvent(e2);
	}

	m_audio_driver->client->process(sampleframes, inputs, outputs, (int)inChannelCount, (int) outChannelCount);

	current_sample_clock += sampleframes;
	SetSampleClock(current_sample_clock);
}

void UIoManager::HandleEvent(SynthEditEvent* e)
{
	// TODO: main loops can run faster if no events to handle. !!!
	// have main container handle this event, forwarded here (like UET_SERVICE_GUI_QUE is) !!!
	switch (e->eventType)
	{
	case UET_IO_FUNC:
	{
		io_func func[2] = { nullptr, nullptr }; // prevent stack overwrite on 32-bit systems.
		// only copies some of function pointer, assumes remainder is zeroed.
		int32_t* i = (int32_t*)&func;
		i[0] = e->parm2;
		i[1] = e->parm3;
		(this->*(func[0]))();
	}
	break;

	case UET_NULL: // null event provided to satisfy condition that que is never empty
		break;

	default:
		assert(false); // audiomaster not expecting events except UET_IO_FUNC
//		EventProcessor::HandleEvent(e);
	};
}

int UIoManager::MIDISetup()
{
	if (!m_midi_driver)
		return 0;

	m_midi_driver->SetErrorCallback([this](const wchar_t* msg) { ErrorMessage(msg); });

	midi_time_correction = 0;

	// Provide the MIDI input callback only if a MIDI input module is registered
	std::function<void(timestamp_t, int, unsigned char*)> onMidiInput;
	if (MidiInUg != nullptr)
	{
		onMidiInput = [this](timestamp_t timestamp, int size, unsigned char* data) {
			OnMidiData(timestamp, size, data);
		};
	}

	int result = m_midi_driver->Setup(CurrentMidiInDevs, CurrentMidiOutDev, m_audio_driver, std::move(onMidiInput));

	if (result != 0)
	{
		on_error();
		return result;
	}

	// Update flags from the driver
	if (m_midi_driver->IsMidiInOpen())
	{
		realtime_flag = true;
		time_offset_midi_in = 0;
	}
	else if (MidiInUg != nullptr && m_inhibit_midi_in_warning == 0)
	{
		m_inhibit_midi_in_warning = 1;
	}

	midi_time_correction = m_midi_driver->GetMidiTimeCorrection();

	return 0;
}

int UIoManager::MidiStop()
{
	if (m_midi_driver)
		return m_midi_driver->Stop();

	return 0;
}
/////////////////////////////////////////////////////////////////////////////

bool UIoManager::RegisterAudioInput()
{
	if(hasAudioInput)
	{
		ErrorMessage(L"You can't use more than 1 Sound In Module, please delete one.");
		return true;
	}

	hasAudioInput = true;
	realtime_flag = true;
	return true;
}

bool UIoManager::RegisterAudioOutput(float p_total_time )
{
	if( p_total_time > 0.0 ) // rendering to disk
	{
//		if( total_record_time < p_total_time )
		{
//			total_record_time = p_total_time;
//			AudioFileOutUg = ug;
		}
	}
	else					// live audio
	{
		if(hasAudioOutput)
		{
			ErrorMessage(L"You have more than 1 Live Audio Out Module, delete all but one.");
			on_error();
			return true;
		}

		hasAudioOutput = realtime_flag = true;
	}

	return false;
}

void UIoManager::StopSoundcard()
{
	/*
	//Stop wave in
	if(hmi != nullptr)
	{
		// check no data waiting
	//		if((imh1->dwFlags & MHDR_DONE) != 0 || (imh2->dwFlags & MHDR_DONE) != 0){
	//			ErrorMessage(L"WAVE in DATA Waiting!");
	//		}
		int res = waveInStop(WaveInHandle);
		if(res != 0 ){
			waveInGetErrorText(res, s, MAXERRORLENGTH);
			ErrorMessage(s);
		}
		res = waveInReset(WaveInHandle);
		if(res != 0 ){
			waveInGetErrorText(res, s, MAXERRORLENGTH);
			ErrorMessage(s);
		}
		res = waveInClose(WaveInHandle);
		if(res != 0 ){
			waveInGetErrorText(res, s, MAXERRORLENGTH);
			ErrorMessage(s);
		}

		for( int i = 0; i < WaveInBufferArray.GetSize() ; i++)
		{
			WaveStreamBuffer &w = WaveInBufferArray[i];
			res = w.UnPrepare Header( WaveInHandle );
			if(res != 0 )
			{
				waveInGetErrorText(res, s, MAXERRORLENGTH);
				ErrorMessage( s );
				end_run();
			}
		}
	}
	*/
	if( m_audio_driver ) // is available
	{
		m_audio_driver->Stop();
	}
}

// Calculate time sync offsets for audio, midi in and/or midi out
void UIoManager::OnFirstSample()
{
}

void UIoManager::TimeSync()
{
	// Get actual time elapsed

	{
		int new_time_offset_audio = m_audio_driver->getOffsetMs();
		// calc conversion between system clock and currently playing sample
		// seems to creep slowly upward, ( mayby soundcard clock vs system clock )

		// Median filter.
		int delta = new_time_offset_audio - time_offset_audio;

		const int smoothZone = 20; // deviation less than this heavily smoothed.
		if (delta > 0)
		{
			if (delta < smoothZone)
			{
				delta = 1;
			}
		}
		else
		{
			if (delta < 0)
			{
				if (delta > -smoothZone)
				{
					delta = -1;
				}
			}
		}

		time_offset_audio += delta;
	}

	// Delegate MIDI time synchronisation to the MIDI driver
	if (m_midi_driver)
	{
		m_midi_driver->TimeSync(
			time_offset_audio,
			latency_ms,
			time_offset_midi,
			midi_time_correction,
			midi_in_timestamp_adjust,
			time_offset_midi_in
		);
	}

	RunAt(SampleClock() + time_sync_interval, static_cast<io_func>(&UIoManager::TimeSync));
}

void UIoManager::play_midi_buffer()
{
	const unsigned int ms_generated = SampleToMs(SampleClock(), AudioDriver()->getSampleRate());

	if (m_midi_driver)
	{
		m_midi_driver->PlayBuffer(ms_generated);
		RunAt(SampleClock() + m_midi_driver->GetBufferSwitchPeriod(), static_cast<io_func>(&UIoManager::play_midi_buffer));
	}
}

// Mechanism to allow ugs to ask to be supplyed MIDI data
void UIoManager::RegisterMidiInput(MidiIn* ug)
{
	if( MidiInUg )
	{
		ErrorMessage(L"You can't use more than 1 Live MIDI In Module, please delete one.");
		return;
	}

	//	realtime_flag = true; // can't set this untill input actually verified available
	MidiInUg = ug;
}

void UIoManager::RegisterMidiOutput(ug_midi_out* ug)
{
	ug->midiOutCallback = [this](timestamp_t p_sample_clock, int msg_size, const unsigned char* midi_bytes) {
		if (m_midi_driver)
		{
			const int ms = SampleToMs(p_sample_clock, (int)AudioDriver()->getSampleRate());
			m_midi_driver->Output(ms, msg_size, midi_bytes);
		}
	};

	realtime_flag = true;
}

int32_t UIoManager::RegisterIoModule(ISpecialIoModule* m)
{
	if (auto midiin = dynamic_cast<MidiIn*>(m); midiin)
	{
		RegisterMidiInput(midiin);
		return 1;
	}

	if (dynamic_cast<ug_soundcard_in*>(m))
	{
		return RegisterAudioInput();
	}

	if (dynamic_cast<ug_soundcard_out*>(m))
	{
		return RegisterAudioOutput();
	}

	if (auto midiout = dynamic_cast<ug_midi_out*>(m); midiout)
	{
		RegisterMidiOutput(midiout);
		return 1;
	}

	return 0;
}

void UIoManager::on_error()
{
	AudioDriver()->on_error();
}

void UIoManager::Render()
{
	StartStream();
	m_audio_driver->Render();
}

// simultaneously start all in and out streams
void UIoManager::StartStream()
{
	if( !StartSoundcard() )
	{
		on_error(); // added, not tested, as SE crashes when DS don't start
		return;// -1;
	}

	if( MIDISetup() ) // power up MIDI input and output
	{
		on_error();
		return;// -1;
	}

	// start timing reference
	system_start_clock = std::chrono::steady_clock::now();

	// startup all in/out streams
	// re-sync timeing approx every 1/4 second
	time_sync_interval = (int)AudioDriver()->getSampleRate() / 4;
	// but attempt to do so at block boundaries to minimise load on main schedular
	const auto driverBlockSize = m_audio_driver->getBufferSize();
	time_sync_interval = time_sync_interval / driverBlockSize; // AudioMaster2()->BlockSize();
	time_sync_interval = std::max( time_sync_interval, 1 ); // can't be zero blocks! (low SR, low latency).
	time_sync_interval *= driverBlockSize; // AudioMaster2()->BlockSize();

	RunAt( time_sync_interval, static_cast<io_func> (&UIoManager::TimeSync) );

	RunAt( 0, static_cast<io_func> (&UIoManager::OnFirstSample) );

	// if directsound active use it as reference time, else use midi out time
	//	add func get_output_time to replace calls to DS time
	// it can use system time minus offset (systeime - DS time ) or systeme - midi time
	//_RPT1(_CRT_WARN, "%u ms digital audio samples have been played so far.\n", output_SampleClock());

	if( m_audio_driver ) // there isn't always audio in/out in a patch
	{
		if( !m_audio_driver->StartStream() )
		{
			ErrorMessage( m_audio_driver->GetLastError() );
			on_error(); // added, not tested, as SE crashes when DS don't start
			return;// -1;
		}
	}

	// Start MIDI streams via the MIDI driver
	if (m_midi_driver)
	{
		m_midi_driver->StartStreams(system_start_clock, time_offset_midi_in);
	}
}

void UIoManager::Stop()
{
	StopSoundcard();
	// MIDI must be stopped last (else if a bufferswitch happens, will attempt to use unprepared MIDI buffers)
	MidiStop();  // no more MIDI
	m_audio_driver_prepared = false;
}

void UIoManager::OnMidiData(timestamp_t timestamp_ms, int size, unsigned char* data)
{
	#if 0 //defined( _DEBUG )
		int elapsed =  timeGetTime() - system_start_clock;
		_RPT4(_CRT_WARN, "OnMidiData : real_time %d ms, MIDI Data %x, time %d (+latency %d ms)\n",elapsed,*((int*) data), (int)timestamp_ms,midi_in_timestamp_adjust );
	#endif

	const int sr = (int)AudioDriver()->getSampleRate();
	timestamp_t ts = msToSamples( ((int) timestamp_ms) + midi_in_timestamp_adjust, sr );

/* todo move to module, this thread should not be reading the moving sample clock
	const timestamp_t graphClock = Midi InUg->SampleClock();
	// Prevent pauses recovering from overload situations where midi timing gets thrown several seconds ahead..
	if(ts < graphClock || ts > graphClock + sr/2 ) // 1/2 second maximum latency / recovery time.
	{
		ts = graphClock;
	}
*/
	if(MidiInUg) // might be rebuilding
		MidiInUg->AddMidiEvent( ts, data, size );
	//_RPT2(_CRT_WARN, "OnMidiData : graphClock %d, MIDI TS %d\n", (int)graphClock, ts);
}

/*
WDM NOTES.

Here is how i call my IOCTL

OVERLAPPED WaitObject;
memset(&WaitObject, 0, sizeof(WaitObject));
WaitObject.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

BOOL retval = DeviceIoControl(m_hDrv,      // handle to a device, file, or
directory
  IOCTL_XYZ,                                              // control code of
operation to perform
  data,                                                            //
pointer to
buffer to supply input data
  */

// LOOK AT "ks.h" for IOCTLs

/*
   From: "Mitchell Rundle" <mitchr@microsoft.com>
Subject: RE: WDM-->CakeWalk:  What can we see in CW10AUD.DLL ?

We are in the process of developing a whitepaper and sample code on this
subject.  Unfortunately we have higher priority tasks on our plate for
the next month.  The gist of streaming to kernel mode is as follows:

Use SetupAPIs to enumerate KS Filters
Use DeviceIoControl to get/set properties and stream data to/from the
filters

Until the white paper and sample code are released, here are 2 ways to
learn more about KS:
1) Use KS Studio.
	a) stream data to the adapter driver using the "test filters" --
this will show you conceptually everything you will need to do in your
code.  If you have logging level turned up high, you can see every IOCTL
sent.
	b) email this group if you have questions on KSStudio
2) Read the DDK (if you haven't already).  The KS IOCTLs are documented
there

Mitch
*/
/*
Dear WDM audio developers,

I can assure you we have wanted to publish information on DirectKS for quite some time.
 As Mitch mentioned earlier on this board we are currently very close to finishing a KSUSER white paper,
 but due to the unprecedented security stand down at Microsoft it's release has been delayed.
 It is hard to say when the paper will be ready but we will finish it as soon as we can.
 We will release this white paper to the http://msdn.microsoft.com/av <http://msdn.microsoft.com/av>  web site when it is done.
 We are also working towards eliminating the need to use these avenues in Longhorn.
 KMixer and the standard audio APIs will be much improved from a latency and flexibility standpoint
 in our next OS release.

Sincerely,


Hakon Strande
Program Manager - Audio Drivers & DDK
Microsoft Corporation
email:  <mailto:hakons@microsoft.com>

  */
/*
1) Enumerating devices (SetupDi or dshow/ICreateDevEnum/IEnumMoniker
with KSCATEGORY_AUDIO). This gives friendlyname and devicepath.
2) CreateFile (use the devicepath from step 1)
3) Querying format support (use handle from step2 and
IOCTL_KS_PROPERTY + ???)
4) Writing buffers (???)
5) Starting/stopping buffers (STATE ???)
6) Driver callback/notification ???

J�rgen Aase
http://www.webmassiva.com


*/

/*
The filter is "SBLive" Wave Device" and I used the ICreateDevEnum
interface to enumerate and get the devicepath which I again pass to
CreateFile to get the filterHandle.

  filterHandle, // from CreateFile
connect, // KSPIN_CONNECT + TKSDATAFORMAT_WAVEFORMATEX
	GENERIC_READ | GENERIC_WRITE,
	pPinHandle // PHANDLE

KsCreatePin returns "The parameter is incorrect"

  Never mind, I got it right;) I forgot to set the KSPIN_CONNECT
interface and medium flags to zero.

Now I have a problem with IOCTL_KS_WRITE_STREAM. After setting the
input sink pin to KSSTATE_PAUSE I want to stream data. I'm confused
what to put in the in-buffer and out-buffer when calling
DeviceIoControl with the pinHandle and IOCTL_KS_WRITE_STREAM. Where
does the KSSTREAM_HEADER go ect? I'm testing diferent soulutions but
the DeviceIoControl never returns.

After setting the input sink to KSSTATE_PAUSE I want to send a
IOCTL_KS_WRITE_STREAM request to the connection (like Grapher.exe).
I'm confused about the what to pass in the in-buffer and out-butter
of DeviceIoControl. Where does the KSSTREAM_HEADER go, and then, what
do I pass in the other buffer?

*/
/*
Subject: RE: IOCTL_KS_WRITE_STREAM completion callback?

DeviceIoControl takes an OVERLAPPED structure as parameter ;-)

When you instantiate your filter using CreateFile you have to
use the FILE_FLAG_OVERLAPPED flag.

> > > I'm trying to stream data from my direct-KS application to a WDM
> > > driver via DeviceIoControl & IOCTL_KS_WRITE_STREAM. I'd expect the
> > > user mode DeviceIoControl call to return on completion of the data
> > > packet (after releasing of the scatter/gather entry in
> the driver).
> > > However, DeviceIoControl returns immediately with
> STATUS_SUCCESS. So
> > > my question is how to implement a callback that is called on
> > > completion of the current data packet?


SONAR calls GetAllocatorFraming to determine the granularity of buffer sizes
we send to the WDM driver.  We may allocate buffers at unaligned multiples,
but the amount of data actually filled into the buffers is always aligned to
the sample size.


*/
#ifdef WDM_DETECTION
void UIoManager::testWDM()
{
	return;
	//	GUID media_subtype_analog = {
	//	/* 6DBA3190-67BD-11CF-A0F7-0020AFD156E4 */
	//	0x6DBA3190, 0x67BD, 0x11CF, {0xA0, 0xF7, 0x00, 0x20, 0xAF, 0xd1, 0x56, 0xe4} };
	std::wstring device_path;
	HRESULT hr = CoInitialize(nullptr);

	if(FAILED(hr))
	{
		_RPT0(_CRT_WARN, "failed init of COM library.\n" );
		return;
	}

	UINT uIndex = 0;
	ICreateDevEnum* pCreateDevEnum = 0;
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC, //CLSCTX_INPROC_SERVER,
	                      IID_ICreateDevEnum, (void**)&pCreateDevEnum);

	switch( hr )
	{
	case S_OK:
		_RPT0(_CRT_WARN, "An instance of the specified object class was successfully created.\n" );
		break;

	case REGDB_E_CLASSNOTREG:
		_RPT0(_CRT_WARN, "A specified class is not registered in the registration database. Also can indicate that the type of server you requested in the CLSCTX enumeration is not registered or the values for the server types in the registry are corrupt. \n" );
		break;

	case CLASS_E_NOAGGREGATION:
		_RPT0(_CRT_WARN, "This class cannot be created as part of an aggregate. \n" );
		break;
	};

	if( pCreateDevEnum == 0 )
		return;

	uIndex = 0;
	IEnumMoniker* pEm;
	hr = pCreateDevEnum->CreateClassEnumerator( AM_KSCATEGORY_RENDER, &pEm, 0);
	pCreateDevEnum->Release();

	if( pEm == 0 ) // indicates none exist
		return;

	ULONG cFetched;
	IMoniker* pM;					// This will access the actual devices
	pEm->Reset();
	IBaseFilter* pACap = nullptr;

	//    while(hr = pEm->Next(1, &pM, &cFetched), hr==S_OK)
	// or just get 1st
	while( pEm->Next(1, &pM, &cFetched) == S_OK)
	{
		IPropertyBag* pPropBag;
		pM->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);
		VARIANT varName;
		varName.vt = VT_BSTR;
		pPropBag->Read(L"FriendlyName", &varName, 0);
		// Do something with the name.
		char achFriendlyName[120];
		WideCharToMultiByte(CP_ACP, 0, varName.bstrVal, -1, achFriendlyName, 80,
		                    nullptr, nullptr);
		_RPT1(_CRT_WARN, "DRIVER - %s\n", achFriendlyName );
		SysFreeString(varName.bstrVal);

		if( achFriendlyName == std::wstring(L"SB Live! Wave Device") )
		{
			pPropBag->Read(L"DevicePath", &varName, 0);
			WideCharToMultiByte(CP_ACP, 0, varName.bstrVal, -1, achFriendlyName, 118,
			                    nullptr, nullptr);
			_RPT1(_CRT_WARN, "PATH - %s\n", achFriendlyName );
			SysFreeString(varName.bstrVal);
			device_path = achFriendlyName;
		}

		if( true )
		{
			// To create and initialize the DirectShow filter that manages the device,
			hr = pM->BindToObject( 0, 0, IID_IBaseFilter, (void**)&pACap);
		}

		// OK , GOT Filter Device, enumerate it's pins
		IEnumPins* ePins;
		pACap->EnumPins( &ePins );
		ULONG fetched = 1;
		IPin* pPin;

		while( ePins->Next(1,&pPin,&fetched) == S_OK )
		{
			PIN_INFO pin_info;
			pPin->QueryPinInfo( &pin_info );
			PIN_DIRECTION dr;
			pPin->QueryDirection( &dr  );
			char name[120]; // "ADC" is the one
			WideCharToMultiByte(CP_ACP, 0, pin_info.achName, -1, name, 80, nullptr, nullptr);
			SysFreeString(varName.bstrVal);
			_RPT1(_CRT_WARN, " PIN %s\n", name );

			if( true )//dr == PINDIR_OUTPUT )
			{
				// check media types
				IEnumMediaTypes* emt;
				AM_MEDIA_TYPE* amt;
				pPin->EnumMediaTypes( &emt );

				while( emt->Next(1,&amt,&fetched) == S_OK )
				{
					if(amt->majortype == MEDIATYPE_Stream )//MEDIATYPE_Audio )//&& amt->subtype == MEDIASUBTYPE_PCM ) //MEDIASUBTYPE_PCM)
					{
						_RPT0(_CRT_WARN, "  MEDIATYPE_Audio\n" );
						// formattype 0F6417D6-C318-11D0-A43F-00A0C9223196 KSDATAFORMAT_SPECIFIER_NONE
						OLECHAR lpsz[100];
						int r = StringFromGUID2(
						            (amt->subtype),  //Interface identifier to be converted
						            lpsz,  //Pointer to the resulting string on return
						            100       //Character count of string at lpsz
						        );
						std::wstring gui = lpsz;
						_RPT1(_CRT_WARN, "     subtype - %s\n", gui );
						r = StringFromGUID2(
						        (amt->formattype),  //Interface identifier to be converted
						        lpsz,  //Pointer to the resulting string on return
						        100       //Character count of string at lpsz
						    );
						gui = lpsz;
						_RPT1(_CRT_WARN, "     Format - %s\n", gui );
					}
				}

				emt->Release();
			}

			pPin->Release();
		}

		ePins->Release();
		//		KsCreatePin
		pPropBag->Release();
		pM->Release();
		uIndex++;
	}

	pEm->Release();

	//AMCap also repeats the process of retrieving the format interface, this time for the audio device.
	//    hr = pBuilder->FindCaptureInterface(pACap,
	//				IID_IAMAudioStreamConfig, (void **)&pASC);
	// OK get device path hopefully
	if( device_path.empty() )
		return;

	//2) IOCreateFile (use the devicepath from step 1)
	SECURITY_ATTRIBUTES saRaw;
	saRaw.nLength=sizeof(saRaw);
	saRaw.lpSecurityDescriptor=nullptr;
	saRaw.bInheritHandle=TRUE;
	HANDLE wdm_out = CreateFile( device_path, GENERIC_WRITE, 0, &saRaw, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if( wdm_out == INVALID_HANDLE_VALUE )
	{
		_RPT0(_CRT_WARN, "Open WDM driver failed\n" );
		return;
	}
	else
	{
		_RPT1(_CRT_WARN, "Open WDM driver OK H%d\n", wdm_out );
	}

	//3) Querying format support (use handle from step2 and
	//IOCTL_KS_PROPERTY + ???)
	// prepare the property structure sent down.
	KSPROPERTY prop;
	prop.Set	= KSPROPSETID_Pin;
	prop.Flags	= KSPROPERTY_TYPE_GET;
	prop.Id		= KSPROPERTY_PIN_CTYPES;
	//	prop.Alignment = 0;
	DWORD dwData;
	ULONG bytes_returned;
	int fSuccess = DeviceIoControl(wdm_out, IOCTL_KS_PROPERTY,&prop,sizeof(prop),&dwData,sizeof(dwData),&bytes_returned, nullptr);

	if( !fSuccess )
	{
		_RPT0(_CRT_WARN, "DeviceIoControl failed\n" );
	}

	int pin_count = dwData;
	_RPT1(_CRT_WARN, "Pins supported %d\n", pin_count );
	KSP_PIN pin_prop;
	pin_prop.Property.Set = KSPROPSETID_Pin;
	pin_prop.Property.Flags = KSPROPERTY_TYPE_GET;
	pin_prop.Property.Id = KSPROPERTY_PIN_DATAFLOW;
	KSPIN_DATAFLOW dataf;

	// have to instansiate first???
	for( int i = 0 ; i < pin_count; i++ )
	{
		pin_prop.PinId = i;
		int fSuccess = DeviceIoControl(wdm_out, IOCTL_KS_PROPERTY,&pin_prop,sizeof(pin_prop),&dataf,sizeof(dataf),&bytes_returned, nullptr);

		if( !fSuccess )
		{
			_RPT0(_CRT_WARN, "DeviceIoControl failed\n" );
		}
		else
		{
			_RPT2(_CRT_WARN, "Pin %d:NECESSARYINSTANCES  %d\n", i, dwData );
		}
	}

	// instansiate pin
	/*
	filterHandle, // from CreateFile
	connect, // KSPIN_CONNECT + KSDATAFORMAT_WAVEFORMATEX
	GENERIC_READ | GENERIC_WRITE,
	pPinHandle // PHANDLE

	KsCreatePin returns "The parameter is incorrect"

	Never mind, I got it right;) I forgot to set the KSPIN_CONNECT
	interface and medium flags to zero. */
	KSPIN_CONNECT kspc;
	kspc.Interface.Set = KSINTERFACESETID_Standard; // GUID
	kspc.Interface.Id = KSINTERFACE_STANDARD_STREAMING; // can also try KSINTERFACE_STANDARD_LOOPED_STREAMING, KSINTERFACE_STANDARD_POSITION
	kspc.Interface.Flags = 0;
	kspc.Medium.Set = KSMEDIUMSETID_Standard;
	kspc.Medium.Id = KSMEDIUM_STANDARD_DEVIO;
	kspc.Medium.Flags = 0;
	kspc.PinId = 3;
	kspc.PinToHandle = 0;
	kspc.Priority.PriorityClass = KSPRIORITY_NORMAL;
	kspc.Priority.PrioritySubClass = 1;
	HANDLE PinHandle;
	//	KSDATAFORMAT dfm;
	KSDATAFORMAT_WAVEFORMATEX dfm;
	dfm.DataFormat.FormatSize = sizeof(KSDATAFORMAT);
	dfm.DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO; //MEDIATYPE_Stream;
	dfm.DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
	dfm.DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX; // specifies type of strucure to follow...
	dfm.DataFormat.Reserved = 0;
	dfm.DataFormat.SampleSize = 4; // ignored here (nchan * 2)
	dfm.DataFormat.Flags = 0;
	dfm.WaveFormatEx.wFormatTag = WAVE_FORMAT_PCM;
	dfm.WaveFormatEx.nChannels = 2;
	dfm.WaveFormatEx.nSamplesPerSec = 44100;
	dfm.WaveFormatEx.wBitsPerSample = 16;
	dfm.WaveFormatEx.nAvgBytesPerSec = (dfm.WaveFormatEx.nSamplesPerSec * dfm.WaveFormatEx.nChannels * dfm.WaveFormatEx.wBitsPerSample) / 8;
	dfm.WaveFormatEx.nBlockAlign = (dfm.WaveFormatEx.nChannels * dfm.WaveFormatEx.wBitsPerSample)/8;
	dfm.WaveFormatEx.cbSize= 0;
	unsigned char    Connect[sizeof(kspc)+sizeof(dfm)];
	KSPIN_CONNECT* cp = (KSPIN_CONNECT*) &Connect;
	KSDATAFORMAT_WAVEFORMATEX* df = (KSDATAFORMAT_WAVEFORMATEX*) (cp + 1);
	*cp = kspc;
	*df = dfm;
	fSuccess = KsCreatePin(wdm_out, (KSPIN_CONNECT*) &Connect, GENERIC_READ | GENERIC_WRITE, &PinHandle );

	if( fSuccess != 0  )
	{
		_RPT0(_CRT_WARN, "Failed to create Pin\n" );
		DWORD err_code = GetLastError();
		LPVOID lpMsgBuf;
		FormatMessage(
		    FORMAT_MESSAGE_ALLOCATE_BUFFER |
		    FORMAT_MESSAGE_FROM_SYSTEM |
		    FORMAT_MESSAGE_IGNORE_INSERTS,
		    nullptr,
		    err_code,
		    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		    (LPTSTR) &lpMsgBuf,
		    0,
		    nullptr
		);
		// Process any inserts in lpMsgBuf.
		// ...
		// Display the string.
		_RPT1(_CRT_WARN, "KsCreatePin() Failed. %s\n", lpMsgBuf );
		// Free the buffer.
		LocalFree( lpMsgBuf );
	}
	else
	{
		// release it
		if( CloseHandle( PinHandle ) == 0 )
		{
			_RPT0(_CRT_WARN, "Close PinHandle failed\n" );
		}
	}

	/*
		fSuccess = DeviceIoControl(wdm_out, IRP_MJ_CREATE,&Connect,sizeof(Connect),&PinHandle,sizeof(PinHandle),&bytes_returned, nullptr);
		if( !fSuccess )
		{
			_RPT0(_CRT_WARN, "IRP_MJ_CREATE DeviceIoControl failed\n" );
		}
	*/
	// IRP_MJ_CREATE
	/*
	IOCTL_KS_READ_STREAM
	Clients use this to read data from a pin.

	IOCTL_KS_WRITE_STREAM
	Clients use this to read data from a pin.
	Handling IOCTL_KS_READ_STREAM and IOCTL_KS_WRITE_STREAM

	Two major control codes or Ioctls, where the driver can get access to the data stream
	are the IOCTL_KS_READ_STREAM and IOCTL_KS_WRITE_STREAM.
	These requests contain a Stream Header list, optionally pointing to the actual stream data.
	Whenever streaming is done under a standard interface, KSSTREAM_HEADER structure is used.


	IOCTL_KS_PROPERTY
	Clients use this IOCTL to get or set properties, or to determine the properties supported by a KS object.
	------------------------------------------------------------
	Take a look at the Virtual Audio Device driver in the XP DDK.
	src\wdm\audio\msvad
	It's an example of a WDM audio driver with no hardware.


	*/
	if( CloseHandle( wdm_out ) == 0 )
	{
		_RPT0(_CRT_WARN, "Close WDM driver failed\n" );
	}
un init COM? conditional on init succeeding.
}
#endif

// from EventProcessor
void UIoManager::AddEvent(SynthEditEvent* p_event)
{
	// added sleeping test to prevent resumeing plugin on every single event
	//if (sleeping && /*p_wake && */(flags & UGF_SUSPENDED) == 0)
	//{
	//	Wake();
	//	assert(!sleeping);
	//}

	// Ensure modules downstream from sender. Unhandled case: [MIDI-CV]->[feedback Delay]->[ADSR2]->[Voice Combiner]->[MIDI-CV] (<-same MIDI-CV).
	assert(/*sleeping ||*/ (timestamp_t)p_event->timeStamp >= SampleClock());

	// note: private_sample_clock used directly, because may be asleep (SampleClock() would assert)
	timestamp_t timeStamp = p_event->timeStamp;
	// new, search from tail, stat change event's go in pin order
	// insert sorted into list
	EventIterator it(events.rbegin());

	while (it != events.rend())
	{
		SynthEditEvent* event2 = *it;

		if ((timestamp_t)event2->timeStamp <= timeStamp) // events with same time must be added in same order received (for stat changes to work properly)
		{
			// most events can be inserted now, sorted only on timestamp.
			if (p_event->eventType < UET_EVENT_SETPIN || p_event->eventType > UET_EVENT_STREAMING_STOP)
			{
				events.insertAfter(it, p_event);
				return;
			}

			// stat change (and streaming change) requires additional sort on pin number
			int pin_idx = p_event->parm1;
			goto short_cut; // jump into loop

			while (it != events.rend()) //pos != nullptr )
			{
				event2 = *it;
			short_cut:
				// If previous event old, insert here. If prev event = Graph-Start, insert after it. Graph-start MUST be first, else oversampler-in overrides initial input plug values.
				if ((timestamp_t)event2->timeStamp < timeStamp || event2->eventType == UET_GRAPH_START)
				{
					events.insertAfter(it, p_event);
					return;
				}

				if (event2->parm1 <= pin_idx && event2->eventType >= UET_EVENT_SETPIN && event2->eventType <= UET_EVENT_STREAMING_STOP)
				{
					events.insertAfter(it, p_event);

					// remove any previous identical stat change
					if (event2->parm1 == pin_idx)
					{
						events.erase(it);
						// remove event2;
						delete_SynthEditEvent(event2);
					}

					return;
				}

				--it;
			}

			goto done;
		}

		--it;
	}

done:
	// list empty
	events.push_front(p_event);
}

void UIoManager::ErrorMessage(std::wstring message)
{
#ifdef _WIN32
	_RPTWN(0, L"%s\n", message.c_str());
#endif
    
	if (m_error_message_callback)
		m_error_message_callback(message.c_str());
}
