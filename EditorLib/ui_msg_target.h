#pragma once

#include "./UniqueSnowflake.h"

namespace gmpi { namespace hosting {
class my_input_stream;
}}

class ui_msg_target : public UniqueSnowflake
{
public:
	virtual void OnDspMsg(int /*p_msg_id*/, gmpi::hosting::my_input_stream& /* p_stream */) {}
};
