#pragma once

#include "Plug.h"
#include "PlugDescriptionDecorator.h"
#include "notify.h"

// basic plug, 'iherits' almost all members from plug description. exception is CUG pointer and connector list

class CPlug4 : public IPlug, public PlugDescriptionDecorator, public Notifier
{
public:
	CPlug4( CUG* p_ug, InterfaceObject* i );
	virtual ~CPlug4();
	IPlug* ConnectedTo() override;

	bool HasActiveConnections() override;
	void PropogateBack(IPlug* plug, int msg_id) override;
	void PropogateForward(IPlug* plug, int msg_id) override;
	void RemoveLines() override;
	void Initialise(bool loaded_from_file = false) override;
	void OnUiDisconnect() override;
	void Disconnect_pt1(CLine2* p_line) override;
	void Disconnect_pt2(CLine2* p_line) override;
	IPlugDescriptionDecorator* AddDecorator( IPlugDescriptionDecorator* p_decorator ) override;
	void RemoveDecorator( IPlugDescriptionDecorator* p_decorator ) override;

	CUG* UG() override
	{
		return cug_;
	}
	void SetUG(CUG* p_ug) override;
	int GetNumConnections() override
	{
		return (int) m_connectors.size();
	}
	connectors_t& Connectors() override
	{
		return m_connectors;
	}
	void AddConnectorQuiet( CLine2* p_line) override
	{
		m_connectors.push_back( p_line );
	}

	// forward all other calls to master plug description...
	// query behaviour

	bool isUnusedSpare() override
	{
		return autoDuplicate() && GetNumConnections() == 0;
	}
	bool canRemove() override
	{
		// If this is a dynamic plug, it may need to be deleted
		return autoDuplicate() && GetNumConnections() == 0;
	}
	bool isCustomisable() override
	{
		return getPlugDescription()->isCustomisable(this);
	}
	int getParameterId() override
	{
		return getPlugDescription()->getParameterId(this);
	}
	bool isUiPlug() override
	{
		return info()->isUiPlug();
	}
	bool isIoPlug() override
	{
		return false;
	}
	bool isUnconnectedIOPlug() override
	{
		return false;
	}
	bool autoDuplicate() override
	{
		return getPlugDescription()->autoDuplicate(this);
	}
	bool IsConnectedTo(IPlug* p_to) override;

	// get properties
	std::wstring getName() override
	{
		return getPlugDescription()->getName(this);
	}
	EPlugDataType getDatatype() override
	{
		return getPlugDescription()->getDatatype(this);
	}
	std::wstring getFileExt() override
	{
		return getPlugDescription()->getFileExt(this);
	}

	std::wstring GetDefaultFormatted();

	std::wstring getDefaultEnumList() override;//{ return getPlugDescription()->getDefaultEnumList(this);};
	EDirection GetDirection() override
	{
		return getPlugDescription()->GetDirection(this);
	}
	void highlightOutsideLines2(int highlightType) override {}
	sRange GetDefaultRange() override
	{
		return getPlugDescription()->GetDefaultRange(this);
	}

	std::wstring GetDefault() override
	{
		return (std::wstring) m_default;
	}
	// set properties
	void SetDefault( const std::wstring& val) override;
	void SetDefaultQuiet( const std::wstring& val) override;
	void setName( const std::wstring& p_name) override
	{
		getPlugDescription()->setName(this, p_name);
	}

	// notification
	void OnNewConnection( CLine2* p_line) override;
	void OnRemove() override {}
	void Notify_GUI( int lHint, void* pHint ) override;

	class TiXmlElement* ExportXml() override;
	void Export(IPlug* self, Json::Value& object_json, int targetType) override;
	void Export(Json::Value& object_json, int targetType) override
	{
		Export(this, object_json, targetType); // forward to above member.
	}

	void Import(tinyxml2::XMLElement* xml, ExportFormatType targetType) override
	{
		Import(this, xml, targetType);
	}
	void Export(tinyxml2::XMLElement* xml, ExportFormatType targetType) override
	{
		Export(this, xml, targetType);
	}

	void Import(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType) override;
	void Export(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType) override;

	CPlug4* Duplicate() override;
	int UniqueID()
	{
		return getPlugDescID();
	}

	int getPlugDescID() override
	{
		return getPlugDescription()->getPlugDescID(this);
	}
	void setPlugDescID( int id ) override
	{
		getPlugDescription()->setPlugDescID( this, id );
	}
	// Need to be factored out
	IPlug* GetUltimateDest2( EDirection dr = DR_IN) override;

	void AdjustDecoratorPointer();

#if defined( _DEBUG )
	std::wstring DebugIdentify();
#endif
	class CContainer* Container(); // handy, non-esential.

	gmpi_forms::State<std::wstring> m_default;

private:
	CUG* cug_;
	connectors_t m_connectors;
};

