

#include <typeinfo>
#include "./UniqueSnowflake.h"
#include "assert.h"
#if _MSC_VER >= 1600 // Only Avail in VS2010 or later.
#include <time.h>
#endif

#ifdef __linux__
#include <stdlib.h>
#endif

using namespace std;
//unique_object_map_t UniqueSnowflake::m_unique_objects;




UniqueSnowflake::UniqueSnowflake() :
	m_unique_handle(-1)
{
	//	setHandleAutoGenerated();
}
/*
int UniqueSnowflake::Handle()
{
	assert( m_unique_handle >= 0 && "must create unique handle first" );
	return m_unique_handle;
}
*/

UniqueSnowflake::~UniqueSnowflake()
{
	//_RPT1(_CRT_WARN, "[[~UniqueSnowflake h=%x\n", m_unique_handle );
	// UnregisterUniqueHandle();
	// no. CUG needs handle during dstrr. assert( m_unique_handle == -2 && "must unregister unique handle first" );
}

UniqueSnowflake* UniqueSnowflakeOwner::HandleToObject( int p_handle )
{
	auto it = m_unique_objects.find( p_handle );
	assert( it != m_unique_objects.end());
	return (*it).second;
}

// Same as above except returns NULL if not found. Used to get parameters during bank load (that may have since been deleted).
UniqueSnowflake* UniqueSnowflakeOwner::HandleToObjectWithNull( int p_handle )
{
	auto it = m_unique_objects.find( p_handle );

	if( it != m_unique_objects.end() )
	{
		return (*it).second;
	}

	return nullptr;
}

bool UniqueSnowflakeOwner::HandleInUse( UniqueSnowflake* snowflake )
{
	auto it = m_unique_objects.find( snowflake->Handle() );
	return( it != m_unique_objects.end() && (*it).second != snowflake );
}

void UniqueSnowflake::setHandle(int p_handle)
{
	m_unique_handle = p_handle;
}

void UniqueSnowflakeOwner::Register( UniqueSnowflake* snowflake )
{
	assert( snowflake->handleIsSet() );

	// add new one
	auto res = m_unique_objects.insert({ snowflake->Handle(), snowflake });

	if( !res.second ) // handle already registered. Happens when pasting existing object.
	{
		// already in. Happens during load old files where ID generated/registered during serialise.
		// Then re-registered in Initialise().
		if( res.first->second == snowflake )
			return;

		// else handle used by other object.
#ifdef _DEBUG
//		int old = snowflake->Handle();
#endif
		setHandleAutoGenerated(snowflake);
//		_RPT1(_CRT_WARN, "                             re-generated. Was %x\n", old );
	}
	else
	{
		//_RPT2(_CRT_WARN, "UniqueSnowflakeOwner::Register '%s' h=%x\n", typeid(*snowflake).name(), snowflake->Handle() );
	}
}

void UniqueSnowflakeOwner::Unregister( UniqueSnowflake* snowflake )
{
	//_RPT1(_CRT_WARN, "]]UniqueSnowflakeOwner::Unregister h=%x\n", snowflake->Handle() );
	if( snowflake->Handle() >= 0 )
	{
		m_unique_objects.erase(snowflake->Handle());

		// m_unique _handle = -1;
		snowflake->setHandle(UniqueSnowflake::DEALLOCATED);
	}
}

// unregister from handle.
void UniqueSnowflakeOwner::Unregister( int handle )
{
	//_RPT1(_CRT_WARN, "]]UniqueSnowflakeOwner::Unregister h=%x\n", handle );
	m_unique_objects.erase(handle);
}

void UniqueSnowflakeOwner::swap( unique_object_map_t& objectList )
{
	m_unique_objects.swap( objectList );
}

void UniqueSnowflakeOwner::setHandleAutoGenerated( UniqueSnowflake* snowflake, bool temporaryHandle )
{
	snowflake->setHandle(GenerateUniqueHandleValue(temporaryHandle) );
	Register(snowflake);
}

UniqueSnowflakeOwner::UniqueSnowflakeOwner()
{
#ifdef _DEBUG
	random_generator.seed((unsigned int)1965);
#else
	random_generator.seed((unsigned int)time(NULL));
#endif
}

// see also SeAudioMaster::AssignTemporaryHandle() - generates non-clashing negative handles.
int UniqueSnowflakeOwner::GenerateUniqueHandleValue(bool temporaryHandle)
{
	int key = 0;
	if(temporaryHandle)
	{
		// Generate nice sequential IDs. Not suitable for regular parameters, if user deletes then adds paramter,
		// new parameter will have old one's ID, and will assume it's identity when loading old Banks.

		// This is useful for Host Controlled Parameters which get created during project load. Using the same ID every time ensures resulting DSP XML is 
		// consistant and comparable each run.
		for(auto it = m_unique_objects.begin(); it != m_unique_objects.end(); ++it)
		{
			assert( (*it).first >= key ); // ensure no one trys ID = -1

			if( (*it).first != key )
				return key;

			assert( (*it).first == key ); // check objects are in order

			key++;
		}
	}
	else
	{
		// New - Random ID gen
		unique_object_map_t::iterator it;

		do
		{
			key = random_generator() & 0x7fffffff; // generate positive integers only.
			assert(key >= 0);
			it = m_unique_objects.find(key);
		} while(it != m_unique_objects.end());
	}

	return key;
}

#ifdef _DEBUG

void UniqueSnowflakeOwner::debugVerify()
{
	// check that all objects in list still exist and have correct handle.
	for( auto it = m_unique_objects.begin() ; it != m_unique_objects.end() ; ++it )
	{
		assert( (*it).first == (*it).second->Handle() );
	}
}
#endif
