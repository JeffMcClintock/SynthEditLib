#pragma once
#include <list>
#include "iterator.h"

class CDocOb;
class it_doc_ob;
class CContainer;

class it_doc_ob_recursive :
	public Iterator
{
public:
	it_doc_ob_recursive(CContainer* p_container);
	virtual ~it_doc_ob_recursive();
	virtual void First();
	virtual void Next();
	virtual bool IsDone();
	virtual CDocOb* CurrentItem();
protected:
	void Clear();
	std::list<it_doc_ob*> iterator_list;
	CContainer* m_container;
};



