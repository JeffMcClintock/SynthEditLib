#pragma once
#include <list>
#include "CUG.h"
#include "CLine2.h"

// iterates all connected pins, including ones connected via container IO mods.

class it_plug_destinations
{
public:
	it_plug_destinations( IPlug* plug );
	virtual ~it_plug_destinations();
	virtual void First();
	it_plug_destinations& operator++()
	{
		Next();
		return *this;
	};
	virtual void Next();
	virtual bool IsDone();
	virtual IPlug* CurrentItem();

	struct connectorIteratorInfo
	{
		connectorIteratorInfo( IPlug* plug )
		{
			from = plug;
			connectors = &(plug->Connectors());
			//pos = 0;//connectors->GetHeadPosition();
			current = 0;
		};
		void First()
		{
			//			pos = connectors->GetHeadPosition();
			it = connectors->begin();
			Next();
		};
		bool IsDone()
		{
			return current == 0;
		};
		void Next()
		{
			if( it == connectors->end() ) //pos == 0 )
			{
				current = 0;
				return;
			}

			CLine2* l = *it; // connectors->GetNext(pos);
			++it;

			if( l->FromPlug == from )
				current = l->ToPlug;
			else
				current = l->FromPlug;
		};
		IPlug* CurrentItem()
		{
			return current;
		};
		connectors_t* connectors;
		connectors_t::iterator it;
		IPlug* current;
		IPlug* from;
	};
protected:
	void SkipUnqualified();
	void Clear();
	std::list<connectorIteratorInfo*> iterator_list;
	IPlug* plug_;
};


