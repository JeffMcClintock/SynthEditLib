 // stdafx is needed as when your using both MFC (std::wstring) and the std C library else get multiply defined symbols (operator new etc) net PRB: LNK2005
#include "conversion.h"
#include "Plug_decorator_namable.h"
#include "Notify_msg.h"
#include "CUG.h"
#include "CContainer.h"
#include "SynthEditDocBase.h"
#include "tinyxml/tinyxml.h"
#include "../tinyXml2/tinyxml2.h"

void Plug_decorator_namable::setName(IPlug* self, const std::wstring& p_name)
{
	m_name = p_name;

	// avoid calling UpdatePlugEnumList on uninitialised graph.
	if( !self->UG()->Document()->isGraphInitialised() )
	{
		return;
	}

	// remove thisdecorator if name set same as default
	if( m_name == getPlugDescription()->getName(self) )
	{
		assert(false);
	}

	self->Notify_GUI(OM_NAME_CHANGE, &m_name);
	self->UG()->Container()->NotifyAllViews2(OM_NAME_CHANGE);
	self->UG()->OnPlugSetName(self);
	self->UG()->Document()->SetModified();
}

void Plug_decorator_namable::Import(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	const char* temp{};
	xml->QueryStringAttribute("name", &temp);
	m_name = Utf8ToWstring(temp);

	getPlugDescription()->Import(self, xml, targetType);
}

void Plug_decorator_namable::Export(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	xml->SetAttribute("name", WStringToUtf8(m_name).c_str() );
	getPlugDescription()->Export(self, xml, targetType);
}

void Plug_decorator_namable::Export(IPlug* self, class Json::Value& object_json, int targetType)
{
	getPlugDescription()->Export(self, object_json, targetType);

	object_json["name"] = WStringToUtf8(m_name);
}
