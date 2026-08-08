
#include <math.h>
#include "cpu_accumulator.h"
#include "CUG.h"
#include "ug_base.h"
#include "Hosting/message_queues.h"

using namespace gmpi::hosting;
using namespace std;

const float smallestAllowedValue = 0.0000001f;

cpu_accumulator::cpu_accumulator() :
	next_val(0)
{
	memset(ModulesActive_, 0, sizeof(ModulesActive_) );

	const float initialValue = log10f(smallestAllowedValue);

	for( int y = 0; y < CPU_HISTORY_COUNT ; y++ )
	{
		values[y] = peaks[y] = initialValue;
	}
}

void cpu_accumulator::staticUpdate(cpu_accumulator* cpu_meter, my_input_stream& p_stream)
{
	float cpu, peak;
	int voiceCount;
	char ModulesActive_[128];

	p_stream >> cpu;
	p_stream >> peak;
	p_stream >> voiceCount;
	assert(voiceCount < sizeof(ModulesActive_));
	p_stream.Read( ModulesActive_, voiceCount );

	if (!cpu_meter)
		return;

	cpu_meter->Update(
		cpu,
		peak,
		voiceCount,
		ModulesActive_
		);
}

void cpu_accumulator::Update(float cpu, float peak, int voiceCount, char* ModulesActive)
{
	std::copy(ModulesActive, ModulesActive + voiceCount, ModulesActive_);
	ModulesActive_[voiceCount] = -1;

	// On first sample, jump to a good estimate.
	if(cpuRunningAverage < 0.0f )
	{
		cpuRunningAverage = cpu;
		cpuRunningMedian = cpuRunningMedianSlow = cpu;
	}

	//	Average medians to give a slower changing text readout.
	const float smoothing = 0.95f;
	cpuRunningAverage = smoothing * cpuRunningAverage + cpu * (1.0f - smoothing);

	const auto stepSize = (std::min)((std::max)(0.00001f, cpuRunningAverage * 0.04f), fabsf(cpu - cpuRunningMedian));
	cpuRunningMedian += copysignf(stepSize, cpu - cpuRunningMedian);

	// more smoothing for text readout
	const auto stepSize2 = (std::min)((std::max)(0.000001f, cpuRunningAverage * 0.005f), fabsf(cpu - cpuRunningMedianSlow));
	cpuRunningMedianSlow += copysignf(stepSize2, cpu - cpuRunningMedianSlow);

	next_val++;
	next_val %= CPU_HISTORY_COUNT;

	// Convert to log scale
	values[next_val] = log10f(max(cpuRunningMedian, smallestAllowedValue));
	peaks[next_val] = log10f(max(peak, smallestAllowedValue));
}
