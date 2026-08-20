// commandMgr.h: interface for the CommandMgr class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_OMMANDMGR_H__0E42A632_9675_11D5_AE89_000103170662__INCLUDED_)
#define AFX_OMMANDMGR_H__0E42A632_9675_11D5_AE89_000103170662__INCLUDED_

#pragma once
#include <memory>
#include <list>
#include <functional>

class CDocOb;
class Checkpoint;
class ApplicationBase;
class CContainer;
class CSynthEditDocBase;

class CommandMgr
{
public:
	CommandMgr(ApplicationBase* p_application);
	virtual ~CommandMgr();
	bool CanRedo();

	void OnCommand(CDocOb* p_object, uint32_t p_command_id, const std::wstring& undo_description);
	void OnMenuCommand(CDocOb* p_object, int p_view_type, int x, int y, uint32_t p_command_id, const std::wstring& p_description = L"");
	bool CanUndo();
	void DoCheckpoint(const std::wstring& p_description);
	void DoCheckpoint(const std::wstring& p_description, std::function<void(CSynthEditDocBase*)> undo, std::function<void(CSynthEditDocBase*)> redo);
	void ClearHistory();
	void Redo();
	void Undo();
	void UpdateMenu();
	bool isUndoEnabled()
	{
		return m_enable_undo;
	}

	bool m_enable_undo{true};
	std::wstring undo_description{ L"Undo" };
	std::wstring redo_description{ L"Redo" };

private:
	void onNewCheckpoint();

	ApplicationBase* m_application{};
	std::list< std::unique_ptr<Checkpoint> > m_cmd_history;
	decltype(m_cmd_history)::iterator m_history_position;
	CContainer* m_active_container{};
};

#endif // !defined(AFX_OMMANDMGR_H__0E42A632_9675_11D5_AE89_000103170662__INCLUDED_)
