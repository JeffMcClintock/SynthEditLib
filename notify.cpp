
#include <assert.h>
#include <algorithm>
// Fix for <sstream> on Mac (sstream uses undefined int_64t)
#include "./modules/se_sdk3/mp_api.h"
#include <sstream>
#include <iomanip>
#include "notify.h"
#include "Notify_msg.h"

#if defined( _DEBUG )
#include "conversion.h"
#endif

using namespace std;

void Notifier::VO_Notify( int lHint, void* pHint )
{
	for_each(
		m_observers.begin(),
		m_observers.end(),
		[this, lHint, pHint](Notifiable* p_observer) { p_observer->OnNotify(this, lHint, pHint); }
	);
}

void Notifier::RegisterObserver(Notifiable* observer)
{
	assert( find(m_observers.begin(), m_observers.end(), observer) == m_observers.end() );
	m_observers.push_back(observer);
	observer->AddWatcher( this );
}

void Notifier::UnRegisterObserver(Notifiable* observer)
{
	/*
		#ifdef _DEBUG
			const char *tname = type id(*this).name(); // workarround some weird bug
			_RPT4(_CRT_WARN, " %25s (%x)::UnRegisterOb <- %25s (%x)\n", tname, this, type id(*observer).name(), observer );
		#endif
	*/
	auto it = find( m_observers.begin(), m_observers.end(), observer );

	if (it != m_observers.end())
	{
		m_observers.erase(it);
		observer->RemoveWatcher(this);
	}
#if 0 //def _DEBUG
	else
	{
		_RPT4(_CRT_WARN, " %25s (%x)::UnRegisterOb <- %25s (%x) WASN'T REGISTERED\n", typeid(*this).name(), this, typeid(*observer).name(), observer );
	}

#endif
}

bool Notifier::IsRegistered(Notifiable* observer)
{
	return find( m_observers.begin(), m_observers.end(), observer ) != m_observers.end();
}

Notifier::~Notifier()
{
	NotifySafe(OM_DELETE);
}

// used when the notification will always result in unregistering.
// involves resetting the iterator
void Notifier::NotifySafe( int p_msg_id )
{
	// improvement. Most views will attempt to unregister,
	// however, because we are iterating the observer list, will cause a crash.
	// modified version fixes this...(and avoids all the complicated workarrounds)
	while( !m_observers.empty() )
	{
#ifdef _DEBUG
		Notifiable* v = m_observers.back();
#endif
		m_observers.back()->OnNotify( this, p_msg_id, 0 );
		assert( m_observers.empty() || m_observers.back() != v );
	}
}

// Slower version, when notification may or may not result in unregistering.
void Notifier::NotifySafe2( int p_msg_id, void* pHint )
{
	vector<Notifiable*> copy_of_observers(m_observers);

	for(auto it = copy_of_observers.begin() ; it != copy_of_observers.end() ; ++it )
	{
		if( IsRegistered( *it ) )
		{
			// object may have been unregisterd indirectly, if so skip.
			(*it)->OnNotify(this, p_msg_id, pHint);
		}
	}
}

Notifiable::~Notifiable()
{
	// unregistering will indirectly remove element from vector
	while( !m_watching.empty() )
	{
		m_watching.back()->UnRegisterObserver( this );
	}
}

void Notifiable::RemoveWatcher( Notifier* p_notifier )
{
#if _HAS_CXX20
	std::erase(m_watching, p_notifier);
#else
	if (auto it = find(m_watching.begin(), m_watching.end(), p_notifier) ; it != m_watching.end())
		m_watching.erase(it);
#endif
}
