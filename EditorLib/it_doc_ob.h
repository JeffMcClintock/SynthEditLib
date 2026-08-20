// it_doc_ob.h: interface for the it_doc_ob class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_it_doc_ob_H__625E6981_9BED_11D5_AE8E_000103170662__INCLUDED_)
#define AFX_it_doc_ob_H__625E6981_9BED_11D5_AE8E_000103170662__INCLUDED_

#pragma once

#include <list>
#include "iterator.h"

class CDocOb;
class CContainer;

class it_doc_ob : public Iterator
{
public:
	it_doc_ob(CContainer* p_cont);
	virtual void First();
	virtual void Next();
	virtual bool IsDone();
	virtual CDocOb* CurrentItem();

private:
	CContainer* m_container;
	std::list<class CDocOb*>::iterator it;
};

#endif // !defined(AFX_it_doc_ob_H__625E6981_9BED_11D5_AE8E_000103170662__INCLUDED_)
