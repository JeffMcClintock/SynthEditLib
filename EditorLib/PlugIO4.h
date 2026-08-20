#pragma once
#include "Plug4.h"

class CPlugIO4 :
	public CPlug4
{
public:
	CPlugIO4( CUG* p_ug = 0, InterfaceObject* i = 0 );
	~CPlugIO4();

	void PropogateBack(IPlug* plug,int msg_id) override;
	void PropogateForward(IPlug* plug, int msg_id) override;
	void OnNewConnection(CLine2* p_line) override;
	void Disconnect_pt2(CLine2* p_line) override;

	void SetTiedTo(CPlugIO4* p)
	{
		TiedTo = p;
	}
	CPlugIO4* GetTiedTo()
	{
		return TiedTo;
	}
	CPlugIO4* AutoTie(EDirection d);
	IPlug* GetUltimateDest2(EDirection dr = DR_IN) override;  // head downsteam first by default.  Pass DR_OUT to head upstream
	void SetDirection(EDirection d)
	{
		m_direction = d;
	}
	EDirection GetDirection() override
	{
		return m_direction;
	}
	EPlugDataType getDatatype() override
	{
		return m_datatype;
	}
	void SetDatatype(EPlugDataType d)
	{
		m_datatype = d;
	}
	void setName( const std::wstring& p_name) override;
	bool isUiPlug() override;

	sRange GetDefaultRange() override;
	std::wstring getDefaultEnumList() override;
	std::wstring getFileExt() override;
	bool isIoPlug() override;
	bool isUnconnectedIOPlug() override;
	bool isUnusedSpare() override;

	void OnRemove() override;
	bool autoDuplicate() override
	{
		return true;
	}
	bool isCustomisable() override
	{
		return true;
	}
	bool canRemove() override;

	void highlightOutsideLines2(int highlightType) override;
	void Export(Json::Value& object_json, int targetType) override;

protected:
	void Export(tinyxml2::XMLElement * pinE, ExportFormatType targetType) override;
	void Import(tinyxml2::XMLElement * pinE, ExportFormatType targetType) override;
private:
	CPlugIO4* TiedTo;
	EDirection m_direction;
	EPlugDataType m_datatype;
};
