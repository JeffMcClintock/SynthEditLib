#pragma once
#include "ug_base.h"
#include "GmpiApiAudio.h"
#include "../Extensions/EmbeddedFile.h"
#include "Extensions/PinCount.h"

class ug_gmpi :
	public ug_base, public gmpi::api::IProcessorHost, public synthedit::IEmbeddedFileSupport, public synthedit::IPinCount
{
public:
	ug_gmpi(class Module_Info* p_moduleType, gmpi::api::IProcessor* p_plugin);

	// IProcessorHost methods
	gmpi::ReturnCode setPin(int32_t blockRelativeTimestamp, int32_t pinId, int32_t size, const void* data) override;
	gmpi::ReturnCode setPinStreaming(int32_t blockRelativeTimestamp, int32_t pinId, bool isStreaming) override;
	gmpi::ReturnCode setLatency(int32_t latency) override;
	gmpi::ReturnCode sleep() override;
	int32_t getBlockSize() override;
	float getSampleRate() override;
	int32_t getHandle() override;

	// IEmbeddedFileSupport
	gmpi::ReturnCode findResourceUri(const char* fileName, /*const char* resourceType,*/ gmpi::api::IString* returnFullUri) override;
	gmpi::ReturnCode registerResourceUri(const char* fullUri) override;
	gmpi::ReturnCode openUri(const char* fullUri, gmpi::api::IUnknown** returnStream) override;
	gmpi::ReturnCode clearResourceUris() override;

	// IPinCount
	int32_t getAutoduplicatePinCount() override;

	// ug_base overides
	ug_base* Create() override;
	ug_base* Clone(CUGLookupList& UGLookupList) override;
	bool BypassPin(UPlug* fromPin, UPlug* toPin) override;
	void DoProcess(int buffer_offset, int sampleframes) override;
	int Open() override;
	void BuildHelperModule() override;
	bool PPGetActiveFlag() override;
	void OnBufferReassigned() override;

#if defined( _DEBUG )
	virtual void DebugPrintName();
#endif

//	void AttachGmpiPlugin(gmpi::api::IProcessor* p_plugin);

	gmpi_sdk::mp_shared_ptr<gmpi::api::IProcessor> plugin_;

protected:
	int localBufferOffset_ = 0;
	void setupBuffers(int bufferOffset);

	// IUnknown methods
	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override;
	GMPI_REFCOUNT_NO_DELETE
};
