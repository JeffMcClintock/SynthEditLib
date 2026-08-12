 // stdafx is needed as when your using both MFC (std::wstring) and the std C library else get multiply defined symbols (operator new etc) net PRB: LNK2005
#include "./Plug_decorator_autoduplicate.h"
#include "tinyxml/tinyxml.h"
#include "../tinyXml2/tinyxml2.h"
#include "Plug.h"

Plug_decorator_autoduplicate::Plug_decorator_autoduplicate( int id ) :
	id_(id)
{
}

int Plug_decorator_autoduplicate::getPlugDescID( IPlug* /*self*/ )
{
	return id_;
}

void Plug_decorator_autoduplicate::setPlugDescID( IPlug* /*self*/, int id )
{
	id_ = id;
}

void Plug_decorator_autoduplicate::Import(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	xml->QueryIntAttribute("id", &id_);

	getPlugDescription()->Import(self, xml, targetType);
}

void Plug_decorator_autoduplicate::Export(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	xml->SetAttribute("id", id_);

	getPlugDescription()->Export(self, xml, targetType);
}

TiXmlElement* Plug_decorator_autoduplicate::ExportXml(IPlug* self)
{
	TiXmlElement* plugElement = getPlugDescription()->ExportXml(self);

	if( !self->isUnusedSpare() ) // avoid last 'spare' autoduplicate plug
	{
		if( plugElement == 0 )
		{
			plugElement = new TiXmlElement( "Plug" );
		}

		plugElement->SetAttribute( "Id", id_ );
	}

	return plugElement;
}

void Plug_decorator_autoduplicate::Export(IPlug* self, class Json::Value& object_json, int targetType)
{
	getPlugDescription()->Export(self, object_json, targetType);

	if (!self->isUnusedSpare()) // avoid last 'spare' autoduplicate plug
	{
		object_json["Id"] = id_;
	}
}
