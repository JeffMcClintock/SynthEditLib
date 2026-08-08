// it_empty.h: interface for the it_empty class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_IT_EMPTY_H__E50CDB53_05FA_11D6_AF04_000103170662__INCLUDED_)
#define AFX_IT_EMPTY_H__E50CDB53_05FA_11D6_AF04_000103170662__INCLUDED_


#pragma once


// this iterator contains no items
template <class T,class DERIVED_FROM> class EmptyIterator : public DERIVED_FROM
{
public:
	EmptyIterator() {}
	virtual ~EmptyIterator() {}
	virtual void First() {}
	virtual void Next() {}
	virtual bool IsDone()
	{
		return true;
	};
	virtual T* CurrentItem()
	{
		return 0;
	};
};

#endif // !defined(AFX_IT_EMPTY_H__E50CDB53_05FA_11D6_AF04_000103170662__INCLUDED_)
