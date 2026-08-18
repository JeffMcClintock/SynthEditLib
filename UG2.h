#pragma once
#include "CUG.h"
#include "it_doc_ob_recursive.h"
#include "SerializationHelper_XML.h"
#include "IPluginGui.h"
#include "Core/GmpiSdkCommon.h"
#include "Core/GmpiApiEditor.h"

class ControllerIterator_SE :
	public gmpi::IMpControllerIterator, gmpi::IMpControllerIteratorItem
{
	it_doc_ob_recursive cugIterator; 

public:
	ControllerIterator_SE(CContainer* topContainer) : cugIterator(topContainer) {}

	// IMpPinIterator support.
	int32_t MP_STDCALL getCount() override
	{
		return 0;
	}
	int32_t MP_STDCALL first() override
	{
		cugIterator.First();
		skipUnqualified();
		return cugIterator.IsDone() ? gmpi::MP_FAIL : gmpi::MP_OK;
	}
	int32_t MP_STDCALL next() override
	{
		cugIterator.Next();
		skipUnqualified();
		return cugIterator.IsDone() ? gmpi::MP_FAIL : gmpi::MP_OK;
	}

	virtual int32_t MP_STDCALL getCurrent(gmpi::IMpControllerIteratorItem** returnCurent) override;

	void skipUnqualified();

	virtual int32_t MP_STDCALL getController(gmpi::IMpController** returnController) override;

	GMPI_QUERYINTERFACE1(gmpi::MP_IID_CONTROLLER_ITERATOR, gmpi::IMpControllerIterator)
	GMPI_REFCOUNT
};

class ControllerHostHelper : public gmpi::api::IControllerHost
{
	class CUG2* plugin{};

public:
	ControllerHostHelper(CUG2* plugin) : plugin(plugin) {}

	// IControllerHost
	gmpi::ReturnCode setParameter(int32_t parameterIndex, gmpi::Field fieldId, int32_t voice, int32_t size, const uint8_t* data) override;

	GMPI_QUERYINTERFACE_METHOD(gmpi::api::IControllerHost);
	GMPI_REFCOUNT_NO_DELETE;
};

class CUG2 :
	public CUG, gmpi::IMpControllerHost, IPluginGui
{
public:
	CUG2( Module_Info* p_type = 0 );
	DECLARE_BUILD_FUNC(CUG2);

	void OnDelete() override;

	gmpi::drawing::RectL getViewObRect(int p_view_type) override;
	void setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect) override;
	void OnDspMsg(int p_msg_id, gmpi::hosting::my_input_stream& p_stream) override;
	void Initialise(bool loaded_from_file) override;

	static std::wstring CopyProtectPlugin( const std::wstring& fname );
	TiXmlElement* ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType) override;

	// IUnknown methods
	GMPI_QUERYINTERFACE1(gmpi::MP_IID_CONTROLLER_HOST, gmpi::IMpControllerHost)
	GMPI_REFCOUNT_NO_DELETE;
		
	// IMpControllerHost
	int32_t MP_STDCALL getHandle(int32_t& returnHandle) override
	{
		returnHandle = CUG::Handle();
		return gmpi::MP_OK;
	}
	void updateParameter(int32_t paramId, int32_t paramFieldType, int32_t voice) override;
	int32_t MP_STDCALL setParameter(int32_t parameterHandle, int32_t paramFieldType, int32_t voice, const void* data, int32_t size) override;
	int32_t getParameterHandle(int32_t moduleHandle, int32_t moduleParameterId) override;
	int32_t getParameterModuleAndParamId(int32_t parameterHandle, int32_t* returnModuleHandle, int32_t* returnModuleParameterId) override;
	int32_t MP_STDCALL setLatency(int32_t latency) override;
	int32_t MP_STDCALL createControllerIterator(gmpi::IMpControllerIterator** returnIterator) override;

	int32_t OnParamUpdateFromHost(int handle, ParameterFieldType field, int voice, const void* data, int32_t size) override;

	int32_t MP_STDCALL pinTransmit(int32_t pinId, int32_t voice, int64_t size, const void* data) override;

	gmpi::IMpController* getController()
	{
		return controller_;
	}

protected:
	template< class Serializer >
	void SerialiseB(Archive2& ar)
	{
		Serializer s(ar.xmlElement);

		s("panelRect", m_rect);
	}

	DECLARE_SERIAL2;

	void preSaveState() override;
	void OnPlugDefaultChange(IPlug* plug) override;

	gmpi::drawing::RectL m_rect; // panel

	gmpi_sdk::mp_shared_ptr<gmpi::IMpController> controller_;
	gmpi::shared_ptr<gmpi::api::IController> controller2_;
	std::map<int32_t, int32_t> parameterToPin;
	int controllerReportedLatency;
	ControllerHostHelper controllerHostHelper;
};
