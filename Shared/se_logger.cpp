#include <atomic>
#include <string>
#include <cstring>
#include "se_logger.h"

namespace se_logger
{

struct Logger
{
	char logdata[500000] = { 0 };
	std::atomic<size_t> write_pos{ 0 }; // next write position (0..capacity)
	std::string filename;

	static Logger& getInstance()
	{
		static Logger instance;
		return instance;
	}
	void logMessage(std::string_view message)
	{
		if(filename.empty()) // easy way to enable logging, set the filename.
			return;

		// Implement logging logic here (e.g., write to a file or console)
		const size_t len = message.size();
		for (;;)
		{
			size_t tail = write_pos.load(std::memory_order_relaxed);

			// not enough room? give up.
			if (tail + len > std::size(logdata))
				return;

			// Reserve [tail, tail+len)
			if (write_pos.compare_exchange_weak(tail, tail + len,
				std::memory_order_acq_rel, std::memory_order_relaxed))
			{
				std::memcpy(logdata + tail, message.data(), len);
				return;
			}
		}
	}

	~Logger()
	{
		if (write_pos > 0 && !filename.empty())
		{
			// on exit, write to file
			if (auto f = fopen(filename.c_str(), "wb"); f)
			{
				fwrite(logdata, 1, write_pos, f);
				fclose(f);
			}
		}
	}
};

void log(std::string_view message)
{
	Logger::getInstance().logMessage(message);
}

void set_log_filename(std::string_view filename)
{
	Logger::getInstance().filename = filename;
}
bool is_log_enabled()
{
	return !Logger::getInstance().filename.empty();
}

} // namespace
