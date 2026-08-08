#pragma once

namespace gmpi { namespace hosting {
class my_input_stream;
}}

class cpu_accumulator
{
public:
	inline static const int CPU_HISTORY_COUNT = 100;

	void Update(float cpu, float peak, int voiceCount, char* ModulesActive);
	cpu_accumulator();

	static void staticUpdate(cpu_accumulator* cpu_meter, gmpi::hosting::my_input_stream& p_stream);

	float values[CPU_HISTORY_COUNT];
	float peaks[CPU_HISTORY_COUNT];
	char ModulesActive_[132]; // 128 + (at least) one. See UgDebugInfo::CpuToGui()
	float cpuRunningAverage = 0.0f;
	float cpuRunningMedian = 0.0f;
	float cpuRunningMedianSlow = 0.0f;
	
	int next_val;
};
