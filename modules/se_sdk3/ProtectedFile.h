#pragma once

#include "mp_api.h"
#include "Drawing.h"
#include <algorithm>

class ProtectedFile2 :
	public gmpi::IProtectedFile2, public gmpi::IProtectedFile
{
	FILE* fileObject;
	std::string uri_;

public:
	static ProtectedFile2* FromUri(const char* fullUri, const char* mode = "rb");

	std::string getFullUri()
	{
		return uri_;
	}

	ProtectedFile2(const char* uri = 0, FILE* pfileObject = 0) :
		fileObject(pfileObject)
		, uri_(uri)
	{
	}
	~ProtectedFile2()
	{
		fclose(fileObject);
	}

	int32_t getSize(int64_t* returnValue) override
	{
		// Save current pos.
		const auto curPos = ftell(fileObject);

		// Query end pos.
		fseek(fileObject, 0, SEEK_END);
		*returnValue = static_cast<int64_t>(ftell(fileObject));

		// return to current pos.
		fseek(fileObject, curPos, SEEK_SET);

		return gmpi::MP_OK;
	}

	int32_t read(void* buffer, int64_t size, int64_t* returnBytesRead = 0) override
	{
		int64_t bytesRead = fread(buffer, 1, (size_t)size, fileObject);
		if (returnBytesRead)
			*returnBytesRead = bytesRead;

		return gmpi::MP_OK;
	}

	int32_t seek(int64_t distanceToMove, int32_t moveMethod, int64_t* newStreamPosition) override
	{
		if (newStreamPosition)
		{
			newStreamPosition = 0;
		}

		int from;
		switch (moveMethod)
		{
		case gmpi::PFST_BEGIN:
			from = SEEK_SET;
			break;
		case gmpi::PFST_CURRENT:
			from = SEEK_CUR;
			break;
		case gmpi::PFST_END:
			from = SEEK_END;
			break;
		default:
			return gmpi::MP_FAIL;
			break;
		}

		fseek(fileObject, (long) distanceToMove, from);
		if (newStreamPosition)
		{
			*newStreamPosition = ftell(fileObject);
		}
		return gmpi::MP_OK;
	}

	// IProtectedFile backward compatibility.
	int32_t close() override
	{
		fclose(fileObject);

		return gmpi::MP_OK;
	}

	int32_t getSize32(int32_t& returnValue) override
	{
		int64_t temp = 0;
		getSize(&temp);
		returnValue = static_cast<int32_t>(temp);

		return gmpi::MP_OK;
	}

	int32_t read(char* buffer, int32_t size) override
	{
		return read((void*) buffer, (int64_t) size);
	}

	GMPI_QUERYINTERFACE1(gmpi::MP_IID_PROTECTED_FILE2, gmpi::IProtectedFile2);
	GMPI_REFCOUNT;
};

class ProtectedMemFile2 :
	public gmpi::IProtectedFile2
{
	std::string data;
	size_t pos;

public:
	ProtectedMemFile2(const char* dat, size_t size) :
		data(dat,size)
		, pos(0)
	{
	}
	ProtectedMemFile2(std::string pdata) :
		data(pdata)
		, pos(0)
	{
	}

	int32_t getSize(int64_t* returnValue) override
	{
		*returnValue = data.size();
		return gmpi::MP_OK;
	}

	int32_t read(void* buffer, int64_t size, int64_t* returnBytesRead = 0) override
	{
		auto todo = (std::min)((size_t) size, data.size() - pos);
		memcpy(buffer, &data[pos], todo);

		if (returnBytesRead)
			*returnBytesRead = todo;

		pos += todo;

		return gmpi::MP_OK;
	}

	int32_t seek(int64_t distanceToMove, int32_t moveMethod, int64_t* newStreamPosition) override
	{
		switch (moveMethod)
		{
		case gmpi::PFST_BEGIN:
			pos = 0;
			break;
		case gmpi::PFST_CURRENT:
			break;
		case gmpi::PFST_END:
			pos = data.size();
			break;
		default:
			return gmpi::MP_FAIL;
			break;
		}

		return gmpi::MP_OK;
	}

	GMPI_QUERYINTERFACE1(gmpi::MP_IID_PROTECTED_FILE2, gmpi::IProtectedFile2);
	GMPI_REFCOUNT;
};

inline ProtectedFile2* ProtectedFile2::FromUri(const char* fullUri, const char* mode)
{
	const char* defaultMode = "rb";

	if (mode == nullptr)
		mode = defaultMode;

	FILE* fileObject = fopen(fullUri, mode);
	if (fileObject)
	{
		return new ProtectedFile2(fullUri, fileObject);
	}
	return 0;
}