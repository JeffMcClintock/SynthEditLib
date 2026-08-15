#pragma once
#include "CUG.h"

class CControl : public CUG
{
public:
	static std::string currentVst3SkinName;

	CControl( Module_Info* p_type = 0 );
	virtual std::wstring getShortDescription();
	gmpi::drawing::RectL getViewObRect(int p_view_type) override;
	void setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect) override;
	void OnPlugDefaultChange(IPlug* plug) override;
	void SetName(const std::wstring& new_name) override;
	void LayoutChange();

	DECLARE_SERIAL2;
	template< class Serializer >
	void SerialiseB(Archive2& ar)
	{
		Serializer s(ar.xmlElement);

		s("panelRect", PanelWndPosition);
	}

	bool show_title_on_panel() override;

protected:

	gmpi::drawing::RectL PanelWndPosition;
};
