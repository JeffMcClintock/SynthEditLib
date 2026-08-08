
#include "it_plug_destinations.h"
#include "it_doc_ob.h"
#include "CContainer.h"
#include "PlugIO4.h"



it_plug_destinations::it_plug_destinations( IPlug* plug ) : plug_(plug)
{
}

it_plug_destinations::~it_plug_destinations()
{
	Clear();
}

void it_plug_destinations::Clear()
{
	while( !iterator_list.empty() )
	{
		delete iterator_list.front();
		iterator_list.pop_front();
	}
}

void it_plug_destinations::First()
{
	Clear();
	iterator_list.push_back( new connectorIteratorInfo(plug_) );
	iterator_list.back()->First();
	SkipUnqualified();
}

void it_plug_destinations::SkipUnqualified()
{
	bool reloop = true;

	while( !iterator_list.empty() && reloop )
	{
		reloop = false;

		if( iterator_list.back()->IsDone() )
		{
			delete iterator_list.back();
			iterator_list.pop_back();

			if( !iterator_list.empty() )
			{
				iterator_list.back()->Next();
				reloop = true;
			}
		}
		else
		{
			CPlugIO4* ioPlug = dynamic_cast<CPlugIO4*>( CurrentItem() );

			if( ioPlug )
			{
				if( ioPlug->GetTiedTo() )
				{
					iterator_list.push_back( new connectorIteratorInfo( ioPlug->GetTiedTo() ) );
					iterator_list.back()->First();
				}
				else // skip un-connected IO plugs altogether.
				{
					iterator_list.back()->Next();
				}

				reloop = true;
			}
		}
	}
}

void it_plug_destinations::Next()
{
	iterator_list.back()->Next();
	SkipUnqualified();
}

bool it_plug_destinations::IsDone()
{
	return iterator_list.empty();
}

IPlug* it_plug_destinations::CurrentItem()
{
	return iterator_list.back()->CurrentItem();
}
