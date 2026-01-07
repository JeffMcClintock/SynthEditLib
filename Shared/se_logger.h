#pragma once
#include <string_view>

/*
#include "Shared/se_logger.h"

se_logger::log("whatever\n");
*/

/*
Logging the Optimus Logic-Pro bug. Instructions for users:

1. Install version of Optimus with logging.
2. In Logic-Pro open a previously saved session which contains Optimus (no more than one copy of Optimus please).
3. If the session fails to load Optimus' preset correctly, then close Logic-Pro (this will write the log file to disk).
4. Locate the log file at ~/Optimus.log, email it to us.

NOTE: We may need to go though several rounds of this procedure because the log might indicate that we need to more detail to understand the issue.
*/

namespace se_logger
{
void log(std::string_view message);

// you can set the filename anytime before the plugin unloads.
void set_log_filename(std::string_view filename);
bool is_log_enabled();
}