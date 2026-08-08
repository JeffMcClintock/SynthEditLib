#if !defined(AFX_OMMAND_H__B18D8A71_39F7_11D6_AF35_000103170662__INCLUDED_)
#define AFX_OMMAND_H__B18D8A71_39F7_11D6_AF35_000103170662__INCLUDED_

#pragma once

#include <string>
#include <functional>
#include <assert.h>
#include "modules/tinyXml2/tinyxml2.h"

class CSynthEditDocBase;

class Checkpoint
{
public:
	virtual ~Checkpoint() {}

	virtual bool canUndo() const
	{
		return false;
	}
	virtual void Undo(CSynthEditDocBase* document) = 0;
	virtual void Redo(CSynthEditDocBase* document) = 0;

	std::wstring description;
	int32_t SelectedViewHandle = -1;
	int32_t SelectedViewType{};
	// scroll?
};

// restore entire document for undo.
class CheckpointFullSerialise : public Checkpoint
{
public:
	CheckpointFullSerialise(class CSynthEditDocBase*, const std::wstring& p_description);

	void Undo(CSynthEditDocBase* document) override;
	void Redo(CSynthEditDocBase* document) override;

private:
	tinyxml2::XMLDocument m_document;
};

// 'inverse-command' for undo. Much faster.
class CheckpointFast : public Checkpoint
{
public:
	CheckpointFast(const std::wstring& p_description, std::function<void(CSynthEditDocBase*)> undo, std::function<void(CSynthEditDocBase*)> redo) :
		undo(undo),
		redo(redo)
	{
		description = p_description;
	}

	bool canUndo() const override
	{
		return true;
	}

	void Undo(CSynthEditDocBase* document) override
	{
		if (undo)
			undo(document);
	}
	void Redo(CSynthEditDocBase* document) override
	{
		assert(redo);
		redo(document);
	}

private:
	std::function<void(CSynthEditDocBase*)> undo;
	std::function<void(CSynthEditDocBase*)> redo;
};


#endif // !defined(AFX_OMMAND_H__B18D8A71_39F7_11D6_AF35_000103170662__INCLUDED_)
