#pragma once
#include <string_view>

namespace se_logger
{
void log(std::string_view message);

// you can set the filename anytime before the plugin unloads.
void set_log_filename(std::string_view filename);
}