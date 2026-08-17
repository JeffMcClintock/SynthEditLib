#pragma once

#include <string>
#include <vector>
#include <list>
#include "DocOb.h"

class IPlug;
class CLineWnd;
class CContainer;

class CLine2 : public CDocOb
{
public:
	friend class CContainer;
	
	// Per-line draw style. Values are persisted in documents, so they are value-stable:
	// 1 was historically ANGLED (straight); it now means "follow the application's
	// 'Default Line Style' preference", which keeps old documents (lineType="1") looking
	// unchanged while letting the user flip all such lines at once.
	enum ELineType{ CURVEY = 0, DEFAULT_STYLE = 1, STRAIGHT = 2 };

    struct Point
    {
        int32_t x;
        int32_t y;
    };
    
protected:  // only serialise can contruct without initialising DocOb correctly
	CLine2();
public:
	CLine2(IPlug* FromPlug, IPlug* ToPlug2);
	~CLine2();

	std::wstring GetName() override
	{
		return L"Line";
	}
	void SetName(const std::wstring&) override {}

	int getLineType()
	{
		return lineType_;
	}
	void setLineType(int lt )
	{
		lineType_ = lt;
	}
	bool HighlightLineTo(class CUG* cug, int32_t flags);
	// Null until Import()/the two-plug constructor resolves them. Import leaves
	// them alone when a module handle isn't in the file, and isValid() is what
	// decides whether the connector survives loading, so they must not start as
	// garbage - see CContainer::ImportChildren().
	IPlug* FromPlug = nullptr;
	IPlug* ToPlug = nullptr;

	//Operations
public:
	bool IsCopyTagged() override;
	virtual void Export(Json::Value& JsonParent, ExportFormatType targetType) override;
	void Export(tinyxml2::XMLElement* moduleElement, ExportFormatType targetType) override;
	void Import(std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement* moduleElement, ExportFormatType targetType) override;

	bool isValid();

	// True when this connector can be written to file. Both ends are stored as an
	// index into the owning module's plug list, so a plug that can't be found there
	// has no representation in the format. Sets whyNot when it returns false.
	bool canSerialize(std::wstring& whyNot);

	template< class Serializer >
	void Serialise2(Serializer& s)
	{
		s("lineType", lineType_);
	}

	bool IsConnectedTo(IPlug* p)
	{
		return FromPlug == p || ToPlug == p;
	}
	void OnPlugDelete( IPlug* p);
	void OnNewConnection();

	gmpi::drawing::RectL getViewObRect(int p_view_type) override;
	void setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect) override;
	void offsetViewObRect(int p_view_type, int dx, int dy) override;
	void Drag(int p_view_type, int dx, int dy) override;

	void InsertNode(int i, double x, double y);
	void DeleteNode(int i);
	void UpdateNode(int i, double x, double y);

	void Straighten();

	std::vector<Point> nodes;
	int lineType_;

private:
	gmpi::drawing::RectL m_struct_rect;
};
