#include "./UG2.h"
#include "tinyxml/tinyxml.h"
#include "UgDatabase.h"
#include "CContainer.h"
#include "PatchManager.h"
#include "PatchParameter.h"
#include "Notify_msg.h"
#include "modules/shared/PatchCables.h"
#include "./SuspendDSP.h"
#include "SynthEditDocBase.h"
#include "Hosting/message_queues.h"

using namespace gmpi::hosting;
using namespace std;

gmpi::ReturnCode ControllerHostHelper::setParameter(int32_t parameterIndex, gmpi::Field fieldId, int32_t voice, int32_t size, const uint8_t* data)
{
	const auto parameterHandle = plugin->get_patch_manager()->getParameterHandle(plugin->Handle(), parameterIndex);

	if (auto param = plugin->get_patch_manager()->GetParameter(parameterHandle); param)
	{
		param->SetValue(RawView(data, size), (ParameterFieldType) fieldId, voice);
		return gmpi::ReturnCode::Ok;
	}

	return gmpi::ReturnCode::Fail;
}


CUG2::CUG2(Module_Info* p_type) : CUG(p_type)
	,m_rect(0,0,0,0)
	, controllerReportedLatency(-1) // -1 indicates "not set" (use default latency from module info).
	, controllerHostHelper(this)
{
}

// given a pin, find it's host-control parameter
PatchParameter_base* getPinParameter(CContainer* container, InterfaceObject* p)
{
	if (!p->isHostControlledPlug())
		return {};
	
	const int hostConnect = p->getHostConnect(); // pass as negative field to identify it as host-connect.

	int32_t attachedToHandle = -1; // = not attached.
	if (AttachesToVoiceContainer((HostControls)hostConnect))
	{
		attachedToHandle = container->getVoiceControlContainer()->Handle();
	}
	else if (AttachesToParentContainer((HostControls)hostConnect)) // oversampling HC
	{
		attachedToHandle = container->Handle();
	}

	auto paramHandle = container->get_patch_manager()->getParameterHandle(attachedToHandle, -1 - hostConnect);

	return container->get_patch_manager()->GetParameter(paramHandle);
}

void CUG2::OnDelete()
{
	// un-register notification on parameters.
	for (auto& it : getType()->controller_plugs)
	{
		auto p = it.second;

		if (p->isHostControlledPlug())
		{
			getPinParameter(Container(), p)->UnRegisterWatcher(this);
		}
	}

	// Remove Patch Cables
	if (!getType()->patchPoints.empty())
	{
		const int parameterIdx = -1 - HC_PATCH_CABLES;
		auto parameterHandle = get_patch_manager()->getParameterHandle(-1, parameterIdx);
		SE2::PatchCables cableList(get_patch_manager()->getParameterValue(parameterHandle));

		bool updated = false;
		for (auto it = cableList.cables.begin(); it != cableList.cables.end(); )
		{
			if ((*it).fromUgHandle == Handle() || (*it).toUgHandle == Handle())
			{
				it = cableList.cables.erase(it);
				updated = true;
			}
			else
			{
				++it;
			}
		}

		// update module parameter holding cable list.
		if (updated)
		{
			auto localToPreventTrashedReturnValue = cableList.Serialise();
			get_patch_manager()->setParameterValue(localToPreventTrashedReturnValue, parameterHandle);
		}
	}

	if (controller_)
		controller_->onDelete();

	CUG::OnDelete();
}

void CUG2::preSaveState()
{
	if (controller_)
		controller_->preSaveState();

	if (controller2_)
		controller2_->syncState();
}

void CUG2::OnPlugDefaultChange(IPlug* plug)
{
	CUG::OnPlugDefaultChange(plug);

	auto defaultValue = WStringToUtf8(plug->GetDefault());

	if (controller_)
		controller_->setPinDefault(plug->isUiPlug() ? 1 : 0, plug->getPlugDescID(), defaultValue.c_str() );
}

gmpi::drawing::RectL CUG2::getViewObRect(int p_view_type)
{
	if (p_view_type == CF_PANEL_VIEW)
	{
		// try to avoid moving panel rect, if we don't show on panel (might mess up paste)
		if ((getType()->GetFlags() & CF_PANEL_VIEW) == 0)
			return {};

		return m_rect;
	}
	else
		return CUG::getViewObRect(p_view_type);
}

void CUG2::setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect)
{
	if( p_view_type == CF_PANEL_VIEW )
	{
		// try to avoid moving panel rect, if we don't show on panel (might mess up paste)
		if( m_rect != p_rect && (getType()->GetFlags()& CF_PANEL_VIEW) != 0)
		{
//			_RPT4(_CRT_WARN, "CUG2::setViewObRect Panel(%d, %d, %d, %d)\n", p_rect.left, p_rect.top, p_rect.right, p_rect.bottom);
			m_rect = p_rect;

			if(Document())
				Document()->SetModified();

			if (Container()) // main view has null container
				Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_PANEL, (void*)this);
		}
	}
	else
		CUG::setViewObRect( p_view_type, p_rect );
}

TiXmlElement* CUG2::ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType)
{
	if (!doExport())
		return 0;

	if (targetType == SAT_VST3_CONTROLERS )
	{
		if (controller_) // TODO: not great, relies on SEM being present to detect controller, better to flag it during XML scan like GUI and Audio categories. i.e. getType()->hasController();
		{
			auto element = new TiXmlElement("ChildController");
			XmlParent->LinkEndChild(element);
			element->SetAttribute("Type", WStringToUtf8(getType()->UniqueId()));
			element->SetAttribute("Handle", Handle());

			// Pin defaults.
			auto pinsE = new TiXmlElement("Pins");
			element->LinkEndChild(pinsE);
			int pinUniqueIdexpected = 0;
			int PinIdx = 0;
			for (auto p : Plugs)
			{
				if (p->GetDirection() == DR_IN )
				{
					auto plugElement = new TiXmlElement("Pin");
					if (PinIdx != pinUniqueIdexpected) // default is zero.
					{
						plugElement->SetAttribute("idx", PinIdx);
						pinUniqueIdexpected = PinIdx + 1;
					}

					pinsE->LinkEndChild(plugElement);
					if(p->isUiPlug())
						plugElement->SetAttribute("type", 1);

					if(!p->GetDefault().empty())
						plugElement->SetAttribute("default", WStringToUtf8(p->GetDefault()));
				}

				++PinIdx;
			}
		}
	}

	return CUG::ExportXml(XmlParent, targetType);
}

void CUG2::updateParameter(int32_t parameterHandle, int32_t paramFieldType, int32_t voice)
{
	if(auto param = get_patch_manager()->GetParameter(parameterHandle); param)
	{
		const auto raw = param->GetValue((ParameterFieldType) paramFieldType, voice);

		controller_->setParameter(parameterHandle, paramFieldType, voice, raw.data(), static_cast<int32_t>(raw.size()));
	}
}

int32_t CUG2::setParameter( int32_t parameterHandle, int32_t paramFieldType, int32_t voice, const void* data, int32_t size)
{
	auto param = get_patch_manager()->GetParameter(parameterHandle);

	if( param != nullptr )
	{
		param->SetValue(RawView(data, size), (ParameterFieldType)paramFieldType, voice);
		return gmpi::MP_OK;
	}

	return gmpi::MP_FAIL;
}

int32_t CUG2::getParameterHandle(int32_t moduleHandle, int32_t moduleParameterId)
{
	return get_patch_manager()->getParameterHandle(moduleHandle, moduleParameterId);
}

int32_t CUG2::getParameterModuleAndParamId(int32_t parameterHandle, int32_t* returnModuleHandle, int32_t* returnModuleParameterId)
{
	return get_patch_manager()->getParameterModuleAndParamId(parameterHandle, returnModuleHandle, returnModuleParameterId);
}


void CUG2::Initialise(bool loaded_from_file)
{
	CUG::Initialise(loaded_from_file);

	if (getType()->isDllAvailable())
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> obj;
		obj.Attach(getType()->Build(gmpi::MP_SUB_TYPE_CONTROLLER, true));

		if( obj )
		{
			// GMPI
			obj->queryInterface(*(const gmpi::MpGuid*)(&gmpi::api::IController::guid), controller2_.put_void());
			if (controller2_)
			{
				controller2_->initialize(&controllerHostHelper, Handle());

				// init parameters.
				auto patch_manager = get_patch_manager();
				for (auto& [parameterIndex, param_info] : getType()->m_parameters)
				{
					auto param = patch_manager->GetParameter(this, parameterIndex);
					param->RegisterWatcher(this);

					constexpr int32_t voice = 0;
					constexpr auto field = gmpi::Field::Value;
					const auto raw = param->GetValue();

					controller2_->setParameter(parameterIndex, field, voice, static_cast<int32_t>(raw.size()), (const uint8_t*) raw.data());
				}
			}

			// SDK3
			obj->queryInterface(gmpi::MP_IID_CONTROLLER, controller_.asIMpUnknownPtr());
			if (controller_)
			{
				controller_->setHost(this);

				// register notification on parameters.
				int i = 0;
				for (auto& it : getType()->controller_plugs)
				{
					auto p = it.second;

					if (p->isParameterPlug())
					{
						auto parameterIndex = p->getParameterId();

						auto param = get_patch_manager()->GetParameter(this, parameterIndex);

						parameterToPin.insert({ param->Handle(), p->getPlugDescID() });
					}
					else if (p->isHostControlledPlug())
					{
						auto param = getPinParameter(Container(), p);
						param->RegisterWatcher(this);

						parameterToPin.insert({ param->Handle(), p->getPlugDescID() });
					}

					++i;
				}

				//* old, indiscriminate. Kept for compatibility with VST2Wrapper (unless it's updated)
				int indx = 0;
				auto p = get_patch_manager()->GetParameter(this, indx);
				while( p != nullptr )
				{
					p->RegisterWatcher(this);
					// possibly don't need to unregister. Param will get deleted when this does anyhow.
					p = get_patch_manager()->GetParameter(this, ++indx);
				}
			}

			if (controller_)
			{
				// Communicate value of all pin defaults.
				for (auto plug : Plugs)
				{
					auto defaultValue = WStringToUtf8(plug->GetDefault());
					controller_->setPinDefault(plug->isUiPlug() ? 1 : 0, plug->getPlugDescID(), defaultValue.c_str());
				}

				controller_->open();
			}
		}
	}
}

int32_t CUG2::OnParamUpdateFromHost(int handle, ParameterFieldType field, int voice, const void* data, int32_t size)
{
	if (field == FT_VALUE && controller_) // TODO use field from XML
	{
		// NEW
		auto it = parameterToPin.find(handle);
		if (it != parameterToPin.end())
		{
			auto pinId = (*it).second;
			controller_->setPin(pinId, voice, size, data);
			controller_->notifyPin(pinId, voice);
		}
	}

	if (controller2_)
	{
		return (int) controller2_->setParameter(handle, (gmpi::Field) field, voice, size, (const uint8_t*)data);
	}
	else if (controller_)
	{
		return controller_->setParameter(handle, field, voice, data, size);
	}

	return 0;
}

int32_t CUG2::pinTransmit(int32_t pinId, int32_t voice, int64_t size, const void* data)
{
	// get param handle from pinId
	for (auto& it : parameterToPin)
	{
		if (it.second == pinId)
		{
			int32_t parameterHandle = it.first;
			int32_t fieldId = FT_VALUE;
			get_patch_manager()->setParameterValue(RawView(data, size), parameterHandle, (gmpi::FieldType) fieldId, voice);
			return gmpi::MP_OK;
		}
	}
	return gmpi::MP_FAIL;
}

int32_t CUG2::createControllerIterator(gmpi::IMpControllerIterator** returnIterator)
{
	auto c = Container();
	while (true)
	{
		if (c->Container() == nullptr)
		{
			*returnIterator = new ControllerIterator_SE(c);
			return gmpi::MP_OK;
		}
		c = c->Container();
	}

	return gmpi::MP_OK;
}

int32_t ControllerIterator_SE::getCurrent(gmpi::IMpControllerIteratorItem** returnCurent)
{
	*returnCurent = this;

	return cugIterator.IsDone() ? gmpi::MP_FAIL : gmpi::MP_OK;
}

void ControllerIterator_SE::skipUnqualified()
{
	while (!cugIterator.IsDone())
	{
		auto ug2 = dynamic_cast<CUG2*>(cugIterator.CurrentItem());
		if (ug2 != nullptr && ug2->getController() != nullptr)
			return;

		cugIterator.Next();
	}
}

int32_t ControllerIterator_SE::getController(gmpi::IMpController** returnController)
{
	*returnController = nullptr;

	if (!cugIterator.IsDone())
	{
		auto ug2 = dynamic_cast<CUG2*>(cugIterator.CurrentItem());
		*returnController = ug2->getController();
	}

	return gmpi::MP_OK;
}

#ifdef _WIN32

typedef vector<WORD> languageIdVector;

BOOL CALLBACK EnumResLangProc(HANDLE hModule,
							  LPCTSTR lpszType,
							  LPCTSTR lpszName,
							  WORD wIDLanguage,
							  LONG_PTR lParam
							 )
{
	languageIdVector* languages= (languageIdVector*) lParam;
	languages->push_back( wIDLanguage );
	return TRUE;
}
#endif

std::wstring CUG2::CopyProtectPlugin( const std::wstring& fname )
{
#ifdef _WIN32

	const int BUFSIZE = 1024;
	HANDLE hFile;
	HANDLE hTempFile;
	DWORD dwRetVal;
	DWORD dwBytesRead;
	DWORD dwBytesWritten;
	DWORD dwBufSize=BUFSIZE;
	UINT uRetVal;
	wchar_t szTempName[BUFSIZE];
	char buffer[BUFSIZE];
	wchar_t lpPathBuffer[BUFSIZE];
	BOOL fSuccess;
	// Open the existing file.
	hFile = CreateFile(fname.c_str(),         // file name
					   GENERIC_READ,          // open for reading
					   FILE_SHARE_READ,       // Enables subsequent open operations on a file or device to request read access.
					   nullptr,                  // default security
					   OPEN_EXISTING,         // existing file only
					   FILE_ATTRIBUTE_NORMAL, // normal file
					   nullptr);                 // no template

	if (hFile == INVALID_HANDLE_VALUE)
	{
		printf ("First CreateFile failed (%d)\n", GetLastError());
		return L"";
	}

	// Get the temp path.
	dwRetVal = GetTempPath(dwBufSize,     // length of the buffer
						   lpPathBuffer); // buffer for path

	if (dwRetVal > dwBufSize || (dwRetVal == 0))
	{
		printf ("GetTempPath failed (%d)\n", GetLastError());
		CloseHandle (hFile);
		return L"";
	}

	// Create a temporary file.
	uRetVal = GetTempFileName(lpPathBuffer, // directory for tmp files
							  TEXT("NEW"),  // temp file name prefix
							  0,            // create unique name
							  szTempName);  // buffer for name

	if (uRetVal == 0)
	{
		printf ("GetTempFileName failed (%d)\n", GetLastError());
		CloseHandle (hFile);
		return L"";
	}

	// Create the new file to write the upper-case version to.
	hTempFile = CreateFile((LPTSTR) szTempName, // file name
						   GENERIC_READ | GENERIC_WRITE, // open r-w
						   0,                    // do not share
						   nullptr,                 // default security
						   CREATE_ALWAYS,        // overwrite existing
						   FILE_ATTRIBUTE_NORMAL,// normal file
						   nullptr);                // no template

	if (hTempFile == INVALID_HANDLE_VALUE)
	{
		printf ("Second CreateFile failed (%d)\n", GetLastError());
		CloseHandle (hFile);
		return L"";
	}

	// Read BUFSIZE blocks to the buffer. Change all characters in
	// the buffer to upper case. Write the buffer to the temporary
	// file.
	do
	{
		if (ReadFile(hFile,
					 buffer,
					 BUFSIZE,
					 &dwBytesRead,
					 nullptr))
		{
			//            CharUpperBuffA(buffer, dwBytesRead);
			fSuccess = WriteFile(hTempFile,
								 buffer,
								 dwBytesRead,
								 &dwBytesWritten,
								 nullptr);

			if (!fSuccess)
			{
				printf ("WriteFile failed (%d)\n", GetLastError());
				CloseHandle (hTempFile);
				CloseHandle (hFile);
				return L"";
			}
		}
		else
		{
			printf ("ReadFile failed (%d)\n", GetLastError());
			CloseHandle (hTempFile);
			CloseHandle (hFile);
			return L"";
		}
	}
	while (dwBytesRead == BUFSIZE);

	// Close the handles to the files.
	fSuccess = CloseHandle (hFile);

	if (!fSuccess)
	{
		printf ("CloseHandle failed (%d)\n", GetLastError());
	}

	fSuccess = CloseHandle (hTempFile);

	if (!fSuccess)
	{
		printf ("CloseHandle failed (%d)\n", GetLastError());
	}

	// Retrieve all langes in SEM
	HINSTANCE hinstLib = LoadLibrary( szTempName );

	// If the handle is valid
	if(hinstLib == nullptr)
	{
		_RPT0(_CRT_WARN, "LoadLibrary FAIL\n" );
	}

	languageIdVector languages;
	BOOL r = EnumResourceLanguages(
				 hinstLib,
				 L"GMPXML",
				 MAKEINTRESOURCE(1),
				 (ENUMRESLANGPROCW) EnumResLangProc,
				 (LONG_PTR) &languages
			 );
	FreeLibrary(hinstLib);
	// now strip XML resources from SEM.
	HANDLE h = BeginUpdateResource(
				   szTempName,					// pointer to executable file name
				   FALSE							// deletion option
			   );

	// remove XML for each language.
	for( size_t l = 0 ; l < languages.size() ; ++ l )
	{
		BOOL r2 = UpdateResource(
					  h,					// update-file handle
					  L"GMPXML",					// address of resource name to update
					  MAKEINTRESOURCE(1),			// address of resource type to update
					  languages[l], //LANG_NEUTRAL,					// language identifier of resource
					  0,							// address of resource data
					  0								// length of resource data, in bytes
				  );

		if( r2 != TRUE )
		{
			DWORD err_code = GetLastError();
			wchar_t* lpMsgBuf;
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
			_RPTW1(_CRT_WARN, L"UpdateResource() Failed. %s\n", lpMsgBuf );
			assert( false && lpMsgBuf );
			// Free the buffer.
			LocalFree( lpMsgBuf );
			return L"";
		}
	}

	r = EndUpdateResource(
			h,							// update-file handle
			FALSE						// write flag
		);
	return szTempName;
#else
    return {};
#endif
}


void CUG2::OnDspMsg(int p_msg_id, gmpi::hosting::my_input_stream& p_stream)
{
	if( p_msg_id == (int) gmpi::hosting::id_to_long("sdk") )
	{
		struct DspMsgInfo2
		{
			int id;
			int size;
			void* data;
			int handle;
		};
		DspMsgInfo2 nfo;
		p_stream >> nfo.id;
		p_stream >> nfo.size;
		nfo.data = malloc(nfo.size);
		p_stream.Read(nfo.data,nfo.size);
		nfo.handle = Handle();

		// Notify Presenters.
		assert(Container()); // main view has null container
		{
			Container()->VO_Notify(OM_ON_DSP_MESSAGE, &nfo);
		}
		free( nfo.data );
	}
	else
	{
		CUG::OnDspMsg( p_msg_id, p_stream );
	}
}

// latency=-1 is used by VST3 wrapper to restart the component because the latency changed.
// the actual latency will be reported by the Processor object.
int32_t MP_STDCALL CUG2::setLatency(int32_t latency)
{
	controllerReportedLatency = latency;

	SuspendDSP x(Document()->Application());

	return gmpi::MP_OK;
}
