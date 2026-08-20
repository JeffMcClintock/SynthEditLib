
#include "it_doc_ob_recursive.h"
#include "it_doc_ob.h"
#include "CContainer.h"

it_doc_ob_recursive::it_doc_ob_recursive(CContainer* p_container) : m_container(p_container)
{
}

it_doc_ob_recursive::~it_doc_ob_recursive()
{
	Clear();
}

void it_doc_ob_recursive::Clear()
{
	while( !iterator_list.empty() )
	{
		delete iterator_list.front();
		iterator_list.pop_front();
	}
}

void it_doc_ob_recursive::First()
{
	Clear();
	iterator_list.push_back(new it_doc_ob(m_container));
	iterator_list.back()->First();

	if( iterator_list.back()->IsDone() )
	{
		delete iterator_list.back();
		iterator_list.pop_back();
	}
}

void it_doc_ob_recursive::Next()
{
	CContainer* cont = dynamic_cast<CContainer*>( CurrentItem() );

	if( cont )
	{
		iterator_list.push_back( new it_doc_ob(cont) );
		iterator_list.back()->First();
	}
	else
	{
		iterator_list.back()->Next();
	}

	while( !iterator_list.empty() && iterator_list.back()->IsDone() )
	{
		delete iterator_list.back();
		iterator_list.pop_back();

		if( !iterator_list.empty() )
		{
			iterator_list.back()->Next();
		}
	}
}

bool it_doc_ob_recursive::IsDone()
{
	return iterator_list.empty();
}

CDocOb* it_doc_ob_recursive::CurrentItem()
{
	return iterator_list.back()->CurrentItem();
}
