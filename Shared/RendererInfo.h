#pragma once
#include <string>
#include "GmpiApiCommon.h"

namespace synthedit
{

// SynthEdit-internal, found by queryInterface on a drawing host; hosts that wrap another host forward it.
// Not a GMPI interface: no refcount, the frame outlives anyone asking.
struct IRendererInfo
{
	// The renderer in use, then each decision that chose it, one per line.
	virtual std::string rendererReport() = 0;

	// {CC7EFA32-D430-45C8-8DA1-9E28FB893B61}
	inline static const gmpi::api::Guid guid =
	{ 0xcc7efa32, 0xd430, 0x45c8, { 0x8d, 0xa1, 0x9e, 0x28, 0xfb, 0x89, 0x3b, 0x61 } };
};

// The host's renderer report, or empty if no host in the chain offers one.
inline std::string queryRendererReport(gmpi::api::IUnknown* host)
{
	void* info{};
	if (host && host->queryInterface(&IRendererInfo::guid, &info) == gmpi::ReturnCode::Ok && info)
		return static_cast<IRendererInfo*>(info)->rendererReport();
	return {};
}

} // namespace synthedit
