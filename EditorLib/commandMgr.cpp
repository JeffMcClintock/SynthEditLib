#include "Application.h" // must be first to fix STL/MFC problems
#include "SynthEditDocBase.h"
#include "commandMgr.h"
#include <chrono>
#include "checkpoint.h"
#include "assert.h"
#include "DocOb.h"
#include "UgDatabase.h"
#include "./SuspendDSP.h"
#include "Notify_msg.h"

#define UNDO_HISTORY_COUNT 40

CommandMgr::CommandMgr(ApplicationBase* p_application) :
	m_history_position(m_cmd_history.begin()),
	m_application(p_application)
{
}

CommandMgr::~CommandMgr(){} // empty destructor in cpp, allows us to omit reference to Checkpoint in header.

bool CommandMgr::CanUndo()
{
	if (m_history_position == m_cmd_history.end())
		return false;

	auto nextHistoryPos = m_history_position;
	nextHistoryPos++;

	return m_enable_undo && nextHistoryPos != m_cmd_history.end();
}

bool CommandMgr::CanRedo()
{
	return m_enable_undo && m_history_position != m_cmd_history.begin();
}

// Pass in command intended for DocOb, this will forward call, then create undo point.
// pass in p_description to creade an 'undo' checkpoint, else leave blank
void CommandMgr::OnMenuCommand(CDocOb* p_object, int p_view_type, int x, int y, uint32_t p_command_id, const std::wstring& p_description)
{
	gmpi::drawing::PointL p{x, y};

	p_object->OnMenuCommand( p_view_type, p_command_id, p );

	if( !p_description.empty() )
	{
		DoCheckpoint(p_description);
	}
}

// Handy short version.
void CommandMgr::OnCommand(CDocOb* p_object, uint32_t p_command_id, const std::wstring& description)
{
	OnMenuCommand(p_object, CF_STRUCTURE_VIEW, -1, -1, p_command_id, description);
}

void CommandMgr::Undo()
{
	if ((*m_history_position)->canUndo())
	{
		(*m_history_position)->Undo(m_application->Document());
		m_history_position++;
	}
	else
	{
		// seek back to last full snapshot
		auto it = m_history_position;
		it++;
		for (; (*it)->canUndo(); it++)
		{
			assert(it != m_cmd_history.end());
		}

		// re-execute all commands except the most recent.
		while (it != m_history_position)
		{
			(*it)->Redo(m_application->Document());
			it--;
		}

		m_history_position++;
	}

	UpdateMenu();
}

void CommandMgr::Redo()
{
	m_history_position--;
	(*m_history_position)->Redo(m_application->Document());

	UpdateMenu();
}

/*
There are two classic approaches to undo: "inverse command" and
   "checkpoint".  In the "inverse command" approach, whenever you execute
   a command on the drawing, like "move", you save the fact that you did
   a "move", along with its arguments on a list.  To undo, you perform
   the "inverse" operation, which would be a move with the arguments negated,
   or however you find it easy to do.  This works provided the functions
   don't have a lot of side effects.  A side effect would occur, for example,
   in a directed-graph type of drawing in which objects are "connected" to
   other objects using lines - a schematic or flowchart, for instance.  When
   you move one object, you may indirectly cause movement or changes of many
   other objects in ways that are difficult to capture because there is no
   direct "command" corresponding to that operation.

   In such cases, I prefer the "checkpoint" approach, in which you make a
   copy of each object before you modify it, then go ahead and do whatever
   operation you want, then save a copy after all operations are done.  The
   undo record then consists of a two-part record for each operation
   performed, which has a "before" list of objects, and an "after" list.
   To undo, you remove the existing copy of the object, and restore the
   "before" copy.  To "redo", you remove the existing copy, and restore the
   "after" copy.

   This may cost more in space, but has the benefit that it works no matter
   what operation you perform, and no matter how many objects it touches.
   Also, you don't have to write anything additional when you add new
   functions.  You do have to write an object-clone constructor, and a
   pointer-mapper, for each new object type you add, however.

   This is a pretty condensed explanation, and I'm sure I've raised more
   questions than I've answered, but perhaps I've launched you in some
   direction.  I've implemented a couple of versions of the "checkpoint"
   approach, and found it works pretty well.  Most apps you see today use
   the "inverse" approach, however.  This is simpler for simple applications,
   but is limited in the kind of operations it can capture.
*/

void CommandMgr::ClearHistory()
{
	m_cmd_history.clear();
	m_history_position = m_cmd_history.end();

	DoCheckpoint(L"Loaded Document");
}

void CommandMgr::DoCheckpoint(const std::wstring& p_description)
{
    if( !m_enable_undo )
        return;
    
#if defined( _DEBUG )
    m_application->Document()->uniqueIdDatabase.debugVerify();
    
    auto startTime = std::chrono::steady_clock::now();
#endif
    
    // if future commands exist in history, delete them, they are no longer relevent
    m_cmd_history.erase(m_cmd_history.begin(), m_history_position);
    
    // store new doc state to undo list
    m_cmd_history.push_front(std::make_unique<CheckpointFullSerialise>(m_application->Document(), p_description));
    
    onNewCheckpoint();
    
#if 0 //def _WIN32
#ifdef _DEBUG
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto elapsedSeconds = 0.001f * (float)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    
    _RPTN(0, "Undo Checkpoint: %s took %.2fs. History size=%d\n", p_description.c_str(), elapsedSeconds, (int)m_cmd_history.size());
#endif
#endif
}

void CommandMgr::DoCheckpoint(const std::wstring& p_description, std::function<void(CSynthEditDocBase*)> undo, std::function<void(CSynthEditDocBase*)> redo)
{
	if( !m_enable_undo )
		return;

#ifdef _DEBUG
	m_application->Document()->uniqueIdDatabase.debugVerify();
	auto startTime = std::chrono::steady_clock::now();
#endif

	// if future commands exist in history, delete them, they are no longer relevent
	m_cmd_history.erase(m_cmd_history.begin(), m_history_position);

	// store new doc state to undo list
	m_cmd_history.push_front(std::make_unique<CheckpointFast>(p_description, undo, redo));

	onNewCheckpoint();

#if 0 //def _WIN32
#ifdef _DEBUG
	auto elapsed = std::chrono::steady_clock::now() - startTime;
	auto elapsedSeconds = 0.001f * (float)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

	_RPTN(0, "Undo Checkpoint: %s took %.2fs. History size=%d\n", p_description.c_str(), elapsedSeconds, (int)m_cmd_history.size());
#endif
#endif
}

void CommandMgr::onNewCheckpoint()
{
	m_cmd_history.front()->SelectedViewHandle = m_application->Document()->SelectedViewHandle;
	m_cmd_history.front()->SelectedViewType = m_application->Document()->SelectedViewType;

	m_history_position = m_cmd_history.begin();

	// trim undo list if too big
	if (m_cmd_history.size() > UNDO_HISTORY_COUNT)
	{
		auto it = m_cmd_history.begin();
		for(int i = 0 ; i < UNDO_HISTORY_COUNT; i++)
		{
			it++;
		}

		while(it != m_cmd_history.end())
		{
			if(!(*it++)->canUndo() ) // i.e. it's a full state snapshot
			{
				// this is a full state snapshot, so we can delete anything after it.
				it = m_cmd_history.erase(it, m_cmd_history.end());
				break;
			}
		}

		assert(m_cmd_history.back()->canUndo() == false); // last item should be a full state snapshot.
	}

	UpdateMenu();
}

// UPDATE UNDO MENU TEXT
void CommandMgr::UpdateMenu()
{
	const auto old_undo_description = undo_description;
	const auto old_redo_description = redo_description;

	undo_description = L"Undo";
	redo_description = L"Redo";

	if(CanUndo())
		undo_description = L"Undo " + (*m_history_position)->description;

	if (CanRedo())
	{
		// get previous item in history, which is the one we will redo.
		// m_history_position is currently at the 'next' item, so we need to go back one.
		auto p_prev = m_history_position;
		p_prev--;

		redo_description = L"Redo " + (*p_prev)->description;
	}

	m_application->NotifyFast(OM_NOTIFY_PROPERTY_CHANGED, (void*) L"UndoEnabled");
	m_application->NotifyFast(OM_NOTIFY_PROPERTY_CHANGED, (void*) L"RedoEnabled");

	if (old_undo_description != undo_description)
	{
		m_application->NotifyFast(OM_NOTIFY_PROPERTY_CHANGED, (void*)L"UndoDescription");
	}
	if (old_redo_description != redo_description)
	{
		m_application->NotifyFast(OM_NOTIFY_PROPERTY_CHANGED, (void*)L"RedoDescription");
	}
}

