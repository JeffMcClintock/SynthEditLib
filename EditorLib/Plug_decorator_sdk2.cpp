
#include "./Plug_decorator_sdk2.h"
#include "./Notify_msg.h"
#include "./CUG.h"
#include "./SynthEditDocBase.h"
#include "CLine2.h"
#include "PatchManager.h"
#include "it_plug_destinations.h"
#include "tinyxml/tinyxml.h"
#include "../tinyXml2/tinyxml2.h"

void Plug_decorator_sdk2::Import(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	xml->QueryIntAttribute("parameterId", &parameterId_);
	getPlugDescription()->Import(self, xml, targetType);
}

void Plug_decorator_sdk2::Export(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	xml->SetAttribute("parameterId", parameterId_);
	getPlugDescription()->Export(self, xml, targetType);
}

void Plug_decorator_sdk2::Initialise( IPlug* self, bool loaded_from_file )
{
	getPlugDescription()->Initialise(self, loaded_from_file);

	// SDK2 GUI plugs need to update auto-enum pin metatdata.
	// NOTE 50% will do so during pin connection (depending on connection order), but the remainder won't.
	if( self->UsesAutoEnumList() )
	{
		self->UG()->UpdatePlugEnumLists();
	}
}

std::wstring Plug_decorator_sdk2::getDefaultEnumList(IPlug* self)
{
	return {};
}

void Plug_decorator_sdk2::setParameterId(int parameterId )
{
	parameterId_ = parameterId;
}

// Fix for DH Text append which has autoduplicating parameter plugs.
int Plug_decorator_sdk2::getParameterId(IPlug* self)
{
	int baseId = getPlugDescription()->getParameterId(self);

	if( baseId > -1 && self->autoDuplicate() )
	{
		return parameterId_;
	}

	return baseId;
}

TiXmlElement* Plug_decorator_sdk2::ExportXml(IPlug* self)
{
	TiXmlElement* plugElement = getPlugDescription()->ExportXml(self);

	int paramid = getParameterId(self);
	if (paramid > -1)
	{
		if( plugElement == 0 )
		{
			plugElement = new TiXmlElement( "Plug" );
		}
		plugElement->SetAttribute("ParameterId", paramid);
	}
	return plugElement;
}

void Plug_decorator_sdk2::Export(IPlug* self, class Json::Value& object_json, int targetType)
{
	getPlugDescription()->Export(self, object_json, targetType);

	int paramid = getParameterId(self);
	if (paramid > -1)
	{
		object_json["parameterId"] = paramid;
	}
}
