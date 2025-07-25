

#include <assert.h>
#include "PatchStorage.h"
#include "my_input_stream.h"
#include "iseshelldsp.h"

#ifdef __linux__
#include <string.h>
#include <stdlib.h>
#endif


// how many patches
#define PP_ARRAY_SIZE 128

PatchStorageVariableSize::PatchStorageVariableSize( int patchCount ) : patchCount_(patchCount)
{
	PatchStorageBase::setPatchCount( patchCount );
	patchMemory_ = new std::pair<int,char*>[patchCount];

	for(int i = 0 ; i < patchCount ; i++ )
	{
		patchMemory_[i].first = 0;
		patchMemory_[i].second = 0;
	}
}

PatchStorageVariableSize::~PatchStorageVariableSize()
{
	for(int i = 0 ; i < patchCount_ ; i++ )
	{
		delete [] patchMemory_[i].second;
	}

	delete [] patchMemory_;
}

void PatchStorageVariableSize::setPatchCount(int newPatchCount)
{
	PatchStorageBase::setPatchCount(newPatchCount);

	assert(newPatchCount == 1 || newPatchCount == 128);
	std::pair<int,char*>* newPatchMemory = new std::pair<int,char*>[newPatchCount];

	int fromPatch = 0;
	int size = patchMemory_[fromPatch].first;

	for( int i = 0 ; i < newPatchCount ; i++ )
	{
		newPatchMemory[i].first = size;
		newPatchMemory[i].second = new char[size];
		memcpy( newPatchMemory[i].second, patchMemory_[fromPatch].second, size );
	}

	for(int i = 0 ; i < patchCount_ ; i++ )
	{
		delete [] patchMemory_[i].second;
	}

	delete [] patchMemory_;
	patchMemory_ = newPatchMemory;
	patchCount_ = newPatchCount;
}


bool PatchStorageVariableSize::SetValue( const void* data, size_t size, int patch )
{
	assert( patch >= 0 && patch < patchCount_ );
	char* dest = patchMemory_[patch].second;
	const bool isChanged = size != patchMemory_[patch].first || (size != 0 && (0 == dest || 0 != memcmp(dest, data, size)));

	// Avoid memory allocation if at all possible.
	if( patchMemory_[patch].first != size )
	{
		delete [] dest;
		patchMemory_[patch].first = (int) size;
		dest = patchMemory_[patch].second = new char[size];
	}

	memcpy( dest, data, size );
	return isChanged;
};

bool PatchStorageVariableSize::SetValue(my_input_stream& p_stream, int patch )
{
	assert( patch >= 0 && patch < patchCount_ );
	const auto originalSize = patchMemory_[patch].first;

	int size;
	p_stream >> size;

	char* dest = patchMemory_[patch].second;

	char temp[8];

	// Avoid memory allocation if at all possible.
	if(originalSize < size)
	{
		delete [] dest;
		dest = patchMemory_[patch].second = new char[size];
	}
	else
	{
		// copy the current value (up to 8 bytes) in order to dertermin if the value changed.
		if (originalSize == size && size <= (int)sizeof(temp))
		{
			memcpy(temp, dest, size);
		}
	}

	patchMemory_[patch].first = size;

	p_stream.Read(dest,size);

	// return true if the value changed. (or value was too big to compare properly)
	return originalSize != size || size > (int)sizeof(temp) || 0 != memcmp(temp, dest, size);
}

RawView PatchStorageVariableSize::GetValueRaw(int patch)
{
	assert(patch > -1 && patch < patchCount_);
	return RawView(patchMemory_[patch].second, patchMemory_[patch].first);
}

PatchStorageFixedSize::PatchStorageFixedSize( int patchCount, int size ) : size_(size)
{
	PatchStorageBase::setPatchCount( patchCount );
	patchMemory_ = new char[size * patchCount];
}

PatchStorageFixedSize::~PatchStorageFixedSize()
{
	delete [] patchMemory_;
}

void PatchStorageFixedSize::setPatchCount(int newPatchCount)
{
	auto patchCount = getPatchCount();
	assert(newPatchCount == 1 || newPatchCount == 128);
	PatchStorageBase::setPatchCount(newPatchCount);

	if( patchCount == newPatchCount )
		return;

	char* newPatchMemory_ = new char[size_ * newPatchCount];

	if(newPatchCount < patchCount)
	{
		memcpy(newPatchMemory_, patchMemory_, size_ * newPatchCount);
	}
	else
	{
		for(int i = 0 ; i < newPatchCount ; i++ )
		{
			char* dest = newPatchMemory_ + i * size_;
			memcpy(dest, patchMemory_, size_ );
		}
	}

	delete [] patchMemory_;
	patchMemory_ = newPatchMemory_;
	patchCount = newPatchCount;
}

bool PatchStorageFixedSize::SetValue(const void* data, size_t size, int patch)
{
	assert( size == size_);

	// when upgrading pre 1.1 files, all patches are loaded, even if 'Ignore PC' is set.
	// This gives user chance to recitfy bug where IPC don't work for CControl-based classes.
	// When DSP patch mem is created however, only one patch is allocated. Need to check GUI
	// ain't updating a patch we don't have.  Happens when loading banks etc.
	if( patch >= 0 && patch < getPatchCount())
	{
		char* dest = patchMemory_ + patch * size_;
		bool isChanged = 0 != memcmp(dest, data, size_);
		memcpy(dest, data, size_);
		return isChanged;
	}

	return false;
};


bool PatchStorageFixedSize::SetValue(my_input_stream& p_stream, int patch )
{
	// when upgrading pre 1.1 files, all patches are loaded, even if 'Ignore PC' is set.
	// This gives user chance to recitfy bug where IPC don't work for CControl-based classes.
	// When DSP patch mem is created however, only one patch is allocated. Need to check GUI
	// ain't updating a patch we don't have.  Happens when loading banks etc.
	if (patch < 0 || patch >= getPatchCount())
		return false;

	char* dest = patchMemory_ + patch * size_;

	// copy the current value (up to 8 bytes) in order to dertermin if the value changed.
	char temp[8];
	if (size_ <= (int)sizeof(temp))
	{
		memcpy(temp, dest, size_);
	}

	p_stream.Read(dest,size_);

	// return true if the value changed. (or value was too big to compare properly)
	return size_ > (int) sizeof(temp) || 0 != memcmp(temp, dest, size_);
}

RawView PatchStorageFixedSize::GetValueRaw(int patch)
{
	const char* dest = patchMemory_ + patch * size_;
	return RawView(dest, size_);
}
