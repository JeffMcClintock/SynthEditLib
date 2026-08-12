#pragma once

#include <assert.h>
#include <string>
#include "srange.h"
#include "SerializationHelper_XML.h"

class IPlug;

namespace tinyxml2
{
	class XMLElement;
}
namespace Json
{
	class Value;
}

class IPlugDescriptionDecorator
{
public:
	// always need
	virtual ~IPlugDescriptionDecorator() {}

	// query behaviour
	virtual bool isCustomisable(IPlug* self) = 0;
	virtual int getParameterId(IPlug* self) = 0;
	virtual bool autoDuplicate(IPlug* self) = 0;

	// get properties
	virtual std::wstring getName(IPlug* self) = 0;
	virtual EPlugDataType getDatatype(IPlug* self) = 0;
	virtual std::wstring getFileExt(IPlug* self) = 0;
//	virtual std::wstring GetDefault(IPlug* self) = 0;
	virtual std::wstring getDefaultEnumList(IPlug* self) = 0;
	virtual EDirection GetDirection(IPlug* self) = 0;
	virtual sRange GetDefaultRange(IPlug* self) = 0;

	// set properties
//	virtual void SetDefault(IPlug* self, const std::wstring& val) = 0;
//	virtual void SetDefaultQuiet(IPlug* self, const std::wstring& val) = 0;
	virtual void setName(IPlug* self, const std::wstring& p_name) = 0;

	// notification
	virtual void DeleteDecorators(IPlug* self) = 0;
	// actions
	virtual void Initialise(IPlug* self, bool loaded_from_file = false ) = 0;// called on first create and serialise

	virtual int getPlugDescID(IPlug* self) = 0;
	virtual void setPlugDescID( IPlug* self, int id ) = 0;

	virtual class TiXmlElement* ExportXml(IPlug* self) = 0;
	virtual void Export(IPlug* self, Json::Value& object_json, int targetType) = 0;
	virtual void Import(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType) = 0;
	virtual void Export(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType) = 0;

	virtual int getDecoratorSortOrder()
	{
		return 100;
	} // must override this.


	virtual IPlugDescriptionDecorator* getPlugDescription() = 0;
	virtual void setPlugDescription(IPlugDescriptionDecorator* p_description) = 0;
};

// default behaviour for a plug desciption decorator is to defer everything to the master plug description
class PlugDescriptionDecorator : public IPlugDescriptionDecorator
{
public:
	// always need
	virtual ~PlugDescriptionDecorator() {}

	// query behaviour
	bool isCustomisable(IPlug* self) override
	{
		return getPlugDescription()->isCustomisable(self);
	}
	int getParameterId(IPlug* self) override
	{
		return getPlugDescription()->getParameterId(self);
	}
	bool autoDuplicate(IPlug* self) override
	{
		return getPlugDescription()->autoDuplicate(self);
	}

	// get properties
	std::wstring getName(IPlug* self) override
	{
		return getPlugDescription()->getName(self);
	}
	EPlugDataType getDatatype(IPlug* self) override
	{
		return getPlugDescription()->getDatatype(self);
	}
	std::wstring getFileExt(IPlug* self) override
	{
		return getPlugDescription()->getFileExt(self);
	}
	//	int GetNumConnections(IPlug *self){ return getPlugDescription()->GetNumConnections(self);};
	//	bool HasActiveConnections(IPlug *self){ return getPlugDescription()->HasActiveConnections(self);};
	//std::wstring GetDefault(IPlug* self) override
	//{
	//	return getPlugDescription()->GetDefault(self);
	//}
	std::wstring getDefaultEnumList(IPlug* self) override
	{
		return getPlugDescription()->getDefaultEnumList(self);
	}
	EDirection GetDirection(IPlug* self) override
	{
		return getPlugDescription()->GetDirection(self);
	}
	sRange GetDefaultRange(IPlug* self) override
	{
		return getPlugDescription()->GetDefaultRange(self);
	}

	// set properties
	//void SetDefault(IPlug* self, const std::wstring& val) override
	//{
	//	getPlugDescription()->SetDefault(self, val);
	//}
	//void SetDefaultQuiet(IPlug* self, const std::wstring& val) override
	//{
	//	getPlugDescription()->SetDefaultQuiet(self, val);
	//}
	void setName(IPlug* self, const std::wstring& p_name) override
	{
		getPlugDescription()->setName(self, p_name);
	}

	// notification
	void DeleteDecorators(IPlug* self) override
	{
		getPlugDescription()->DeleteDecorators(self);
		delete this;
	}

	// actions
	void Initialise(IPlug* self, bool loaded_from_file = false ) override
	{
		getPlugDescription()->Initialise(self, loaded_from_file);
	} // called on first create and serialise
	int getPlugDescID(IPlug* self) override
	{
		return getPlugDescription()->getPlugDescID(self);
	}
	void setPlugDescID( IPlug* self, int id ) override
	{
		getPlugDescription()->setPlugDescID( self, id );
	}

	class TiXmlElement* ExportXml(IPlug* self) override
	{
		return getPlugDescription()->ExportXml(self);
	}
	// default behaviour is to do nothing and pass on to next decorator.
	void Export(IPlug* self, class Json::Value& object_json, int targetType) override
	{
		getPlugDescription()->Export(self, object_json, targetType);
	}

	void Import(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType) override
	{
		assert(false); // override this please.
		getPlugDescription()->Import(self, xml, targetType);
	}
	void Export(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType) override
	{
		assert(false); // override this please.
		getPlugDescription()->Export(self, xml, targetType);
	}

	// must override. int getDecoratorSortOrder(){return getPlugDescription()->getDecoratorSortOrder();};

	PlugDescriptionDecorator(IPlugDescriptionDecorator* p_pin_description = 0) : m_pin_description(p_pin_description) {}
	IPlugDescriptionDecorator* getPlugDescription() override
	{
		return m_pin_description;
	}
	void setPlugDescription(IPlugDescriptionDecorator* p_description) override
	{
		m_pin_description = p_description;
	}

private:
	IPlugDescriptionDecorator* m_pin_description = {};
};

