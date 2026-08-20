#include "it_doc_ob.h"
#include "CContainer.h"
#include "assert.h"

it_doc_ob::it_doc_ob(CContainer* p_cont) :
	m_container(p_cont)
{
}

void it_doc_ob::First()
{
	it = m_container->BaseList.begin();
}

void it_doc_ob::Next()
{
	++it;
}

bool it_doc_ob::IsDone()
{
	return it == m_container->BaseList.end();
}

CDocOb* it_doc_ob::CurrentItem()
{
	return *it;
}
