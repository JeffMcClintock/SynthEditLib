#pragma once
#include "jsoncpp/json/json.h"

#include <string>
#include <list>
#include "./modules/se_sdk2/se_datatypes.h"
#include "HostControls.h"
#include "DocOb.h"
#include "InterfaceObject.h"

namespace tinyxml2
{
	class XMLElement;
}

// bool areEquivalentDefaults(const std::wstring& a, const std::wstring& b, EPlugDataType dtype);

typedef	std::list<class CLine2*> connectors_t;
class CPlug4;

class IPlug
{
protected:
	class InterfaceObject* info_ = {};

public:
	IPlug(InterfaceObject* pinfo) : info_(pinfo) {}
	virtual ~IPlug() {}

	InterfaceObject* info() { return info_; }

	// methods which can be overriden by decorators
	virtual int getPlugDescID() = 0;
	virtual void setPlugDescID(int id) = 0;		// autoduplicate plugs only (SDK3).
	virtual class TiXmlElement* ExportXml() = 0;
	virtual void Export(class Json::Value& object_json, int targetType) = 0;
	virtual void Import(tinyxml2::XMLElement* xml, ExportFormatType targetType) = 0;
	virtual void Export(tinyxml2::XMLElement* xml, ExportFormatType targetType) = 0;
	virtual std::wstring GetDefault() = 0;
	virtual void SetDefault(const std::wstring& val) = 0;
	virtual void SetDefaultQuiet(const std::wstring& val) = 0;
	virtual void setName(const std::wstring& p_name) = 0;
	virtual std::wstring getName() = 0;
	virtual int getParameterId() = 0;
	virtual void Initialise(bool loaded_from_file = false) = 0; // called on first create and serialise

	// methods which can be overriden by IO pins
	virtual void PropogateBack(IPlug* plug, int msg_id) = 0;
	virtual void PropogateForward(IPlug* plug, int msg_id) = 0;
	virtual void OnNewConnection(CLine2* p_line) = 0;
	virtual void Disconnect_pt2(CLine2* p_line) = 0;
	virtual EDirection GetDirection() = 0;
	// Need to be factored out
	virtual IPlug* GetUltimateDest2(EDirection dr = DR_IN) = 0;  // hacky?, used to shortcut proper methods. should be private to CPlug2. !!!!
	virtual EPlugDataType getDatatype() = 0;
	virtual bool isUiPlug() = 0;
	virtual struct sRange GetDefaultRange() = 0;
	virtual std::wstring getDefaultEnumList() = 0;
	virtual std::wstring getFileExt() = 0;
	virtual bool isIoPlug() = 0;
	virtual bool isUnconnectedIOPlug() = 0;
	virtual bool isUnusedSpare() = 0;
	virtual void OnRemove() = 0;
	virtual bool autoDuplicate() = 0;
	virtual bool isCustomisable() = 0;
	virtual bool canRemove() = 0;

	// query behavior
	bool can_set_value()
	{
		// change: can't set default on DR_OUT GUI plug (PAtchMem Float fix).
		if (getDatatype() == DT_MIDI)
			return false;

		// SDK2 GUI modules have had pins reversed (SE 1.1) so rules are different.
		bool actsAsInput;

		//* prevents my 'new' built-in sub-controls working
		if (isOldStyleGuiPlug())
		{
			actsAsInput = GetDirection() != DR_IN;
		}
		else
		{
			actsAsInput = GetDirection() == DR_IN;
		}

		return !isUnusedSpare() &&
			(actsAsInput || isSettableOutput());
	}

	bool can_rename()
	{
		return isRenamable() && !isUnusedSpare();
	}
	bool canAcceptConnection()
	{
		if (GetNumConnections() == 0 || getDatatype() == DT_FSAMPLE || getDatatype() == DT_MIDI2)
		{
			return true;
		}

		return GetDirection() == DR_OUT;
	}
	bool SetsOwnValue()
	{
		// input plugs with no connections need to set their own value
		// output plugs in 'fixed values' ug also set their own value
		return (GetDirection() == DR_IN && !HasActiveConnections()) || isSettableOutput();
	}
	bool GetVisible()
	{
		if (GetNumConnections() > 0)
			return true;

		const bool obsolete = DisableIfNotConnected() || (info()->GetFlags() & (IO_PARAMETER_SCREEN_ONLY)) != 0;
		return !isMinimised() && !obsolete;
	}
	bool is_filename(){return info()->is_filename();}
	bool isRenamable() { return info()->isRenamable(); }
	bool isParameterPlug() { return info()->isParameterPlug(); }
	bool isHostControlledPlug() { return info()->isHostControlledPlug(); }
	bool isPolyphonic() { return info()->isPolyphonic(); }
	bool isPolyphonicGate() { return info()->isPolyphonicGate(); }
	int getParameterFieldId() { return info()->getParameterFieldId(); }
	HostControls getHostConnect() { return info()->getHostConnect(); }
	bool isOldStyleGuiPlug() { return info()->isOldStyleGuiPlug(); }
	bool isMinimised() { return info()->isMinimised(); }
	bool autoConfigureParameter() { return info()->autoConfigureParameter(); }
	bool isSettableOutput() { return info()->isSettableOutput(); }
	bool RedrawOnChange() { return info()->RedrawOnChange(); }
	bool DisableIfNotConnected() { return info()->DisableIfNotConnected(); }
	const std::string& getClassName() { return info()->getClassName(); }
	std::wstring GetComments() { return info()->GetComments(); }
	bool UsesAutoEnumList()
	{
		return getDatatype() == DT_ENUM
			&& !isUiPlug()
			&& info()->GetEnumList() == L"{AUTO}";
	}

	// get properties
	virtual class CUG* UG() = 0;
	virtual IPlug* ConnectedTo() = 0;
	virtual int GetNumConnections() = 0;
	virtual bool HasActiveConnections() = 0;
	virtual bool IsConnectedTo(IPlug* p_to) = 0;
	virtual void highlightOutsideLines2(int highlightType) = 0;

	virtual connectors_t& Connectors() = 0;

	// set properties
	virtual void SetUG(CUG* p_ug) = 0;

	// notification
	virtual void OnUiDisconnect() = 0;

	// actions
	virtual void Disconnect_pt1(CLine2* p_line) = 0;
	virtual void RemoveLines() = 0;
	virtual CPlug4* Duplicate() = 0;
	virtual class IPlugDescriptionDecorator* AddDecorator( IPlugDescriptionDecorator* p_decorator ) = 0;
	virtual void RemoveDecorator( IPlugDescriptionDecorator* p_decorator ) = 0;
	virtual void Notify_GUI( int lHint, void* pHint ) = 0;

	virtual void AddConnectorQuiet(CLine2* p_line) = 0;

};

// non-member functions
bool PlugSortOrderOK( IPlug* p_first, IPlug* p_second );

bool canConnectplugs(IPlug* p_from, IPlug* p_to, std::wstring& p_error_msg );
class CDocOb* ConnectPlugs( IPlug* p_from, IPlug* p_to, int32_t handle);
void MoveLinesFrom( IPlug* old_plug, IPlug* new_plug );