
#include <vector>
#include <sstream>
#include "SynthEditDocBase.h"
#include "SynthEditAppBase.h"
#include "CUG.h"
#include "module_info.h"

struct moduleIdentity
{
	int32_t handle;
	int32_t voice;

	bool operator< (const moduleIdentity& c1) const
	{
		return c1.handle == this->handle ? c1.voice < this->voice : c1.handle < this->handle;
	}

	bool operator== (const moduleIdentity& c1) const
	{
		return c1.handle == this->handle && c1.voice == this->voice;
	}
};

struct pin_an
{
	std::vector< void* > toModuleAddress;
	std::vector<float> audioData;
};

struct mod_an
{
	void* address = {};
	int32_t handle = {};
	int32_t voice = {};
	std::vector< pin_an > pins;
};

struct inouterror
{
	moduleIdentity moduleId;
	float inError = -300;
	float outError = -300;
};


int serializeCancelationSnapshot(const char* filename, std::vector<mod_an>& results)
{
	auto file = fopen(filename, "rb");
	if (!file)
	{
//		_RPT0(0, "Cancelation: CAN'T OPEN FILE\n");

		return -1;
	}

	int32_t blockSize = 0;
	fread(&blockSize, sizeof(blockSize), 1, file);

	while (true)
	{
		void* address = {};
		auto r = fread(&address, sizeof(address), 1, file);

		if (r < 1)
			break;

		int32_t handle = 0;
		r = fread(&handle, sizeof(handle), 1, file);

		if (r < 1)
			break;

		int32_t voice = 0;
		r = fread(&voice, sizeof(voice), 1, file);

		results.push_back({ address, handle, voice, {} });

		auto& module_an = results.back();

		// Write pin count
		int32_t pinCount = 0;
		fread(&pinCount, sizeof(pinCount), 1, file);

		for (int i = 0; i < pinCount; ++i)
		{
			int32_t connectionsCount = 0;
			if (0 >= fread(&connectionsCount, sizeof(connectionsCount), 1, file))
				return blockSize;

			module_an.pins.push_back({});
			auto& currentPinData = module_an.pins.back();

			if (connectionsCount) // if no connections, don't bother with signal.
			{
				// read destination modules handles
				for (int j = 0; j < connectionsCount; ++j)
				{
					void* destAddress = {};
					if (0 >= fread(&destAddress, sizeof(destAddress), 1, file))
						return blockSize;

					currentPinData.toModuleAddress.push_back(destAddress);
				}

				currentPinData.audioData.resize(blockSize);
				if (0 >= fread(currentPinData.audioData.data(), sizeof(float), blockSize, file))
					return blockSize;
			}
		}
	}

	fclose(file);

	return blockSize;
}

// the error on each outgoing pin.
struct cancelCompare
{
	std::vector< pin_an >* pinsA = {};
	std::vector< pin_an >* pinsB = {};
};

std::pair<int32_t, int32_t> lookupHandleFromAddress(void* address, const std::vector<mod_an>& results)
{
	for (auto& r : results)
	{
		if (r.address == address)
		{
			return { r.handle, r.voice };
		}
	}

	return { -1, -1 };
}

moduleIdentity lookupHandleFromAddress2(void* address, std::vector<mod_an>& results)
{
	for (auto& r : results)
	{
		if (r.address == address)
		{
			return { r.handle, r.voice };
		}
	}

	return { -1, -1 };
}

struct pinIdentity
{
	int32_t handle;
	int32_t voice;
	int32_t pinIdx;

	bool operator< (const pinIdentity& c1) const
	{
		if (c1.handle != this->handle)
			return c1.handle < this->handle;

		if (c1.voice != this->voice)
			return c1.voice < this->voice;

		return c1.pinIdx < this->pinIdx;
	}
};

void printAudioData(char AorB, const std::vector<float>& data)
{
#ifdef _WIN32
#ifdef _DEBUG
	bool allSame = true;
	for (auto f : data)
	{
		allSame &= f == data[0];
	}

	_RPT1(0, "    %c{", AorB);
	if (allSame)
	{
		_RPT1(0, "%10f", data[0]);
	}
	else
	{
		for (auto f : data)
		{
			_RPT1(0, "%10f, ", f);
		}
	}
	_RPT0(0, "}\n");
#endif
#endif
}

std::vector< std::pair< moduleIdentity, std::string > > getOrphans(const std::vector<mod_an>& results)
{
	std::vector< std::pair< moduleIdentity, std::string > > orphans;
	std::map< std::string, int > uniqueness;

	for (const auto& r : results)
	{
		if (r.handle < 0 && r.handle > -10000 && !r.pins.empty())
		{
//			_RPT1(0, " %9d/%2d ->\n            ", r.handle, r.voice);
			std::string signature;
			int i = 0;
			for (const auto& p : r.pins)
			{
				signature += std::to_string(i) + ":";
				for (const auto& dest : p.toModuleAddress)
				{
					auto [toHandle, toVoice] = lookupHandleFromAddress(dest, results);
					signature += std::to_string(toHandle) + "/" + std::to_string(toVoice) + ",";
//					_RPT1(0, "%d, ", toHandle);
				}
//				_RPT0(0, "\n");
			}
			orphans.push_back({ {r.handle,r.voice}, signature });
			//			_RPT0(0, "\n");
			
			uniqueness[signature] = uniqueness[signature] + 1;
		}
	}

	// remove duplicates
	for (const auto& d : uniqueness)
	{
		if (d.second > 1)
		{
			const auto& signature = d.first;
			orphans.erase(std::remove_if(orphans.begin(), 
                              orphans.end(),
                              [signature](std::pair< moduleIdentity, std::string >& u)
								{
									return u.second == signature;
								}),
				orphans.end()
			);
		}
	}

	return orphans;
}

void CancellationAnalyse(CSynthEditAppBase* app)
{
#if defined( _DEBUG )

	const char* filenameA = "C:\\temp\\cancellation\\SE16\\snapshotA.raw";
	const char* filenameB = "C:\\temp\\cancellation\\SE16\\snapshotB.raw";

	std::vector<mod_an> resultsA;
	const auto blockSize = serializeCancelationSnapshot(filenameA, resultsA);
	if (blockSize <= 0)
		return;

	std::vector<mod_an> resultsB;
	const auto blockSizeB = serializeCancelationSnapshot(filenameB, resultsB);

	if (blockSize != blockSizeB)
	{
//		_RPT0(0, "The two snapshots should be captured at the same blocksize.\n");
		assert(blockSize == blockSizeB);
		return;
	}

//	_RPT2(0, "Snapshot A: %d pins. Snapshot B: %d pins.\n", resultsA.size(), resultsB.size());

#if 0
	_RPT0(0, "======================== FILE A =================================\n");
	for (auto& md : resultsA)
	{
		auto cur_module = dynamic_cast<CUG*>(app->Document()->uniqueIdDatabase.HandleToObjectWithNull(md.handle));

		std::wstring moduleName;
		if (cur_module)
		{
			moduleName = cur_module->GetName();
			if (moduleName.empty())
			{
				moduleName = cur_module->getType()->UniqueId();
			}
		}

		_RPT2(0, "0x08%x : %S\n", md.handle, moduleName.c_str());
		int index = 0;
		for (auto pd : md.pinData)
		{
			_RPT1(0, " %3d: ", index);
			for (int i = 0; i < 5; ++i)
			{
				_RPT1(0, "%f, ", pd[i]);
			}
			_RPT0(0, "\n");

			++index;
		}
		_RPT0(0, "\n");
	}
	_RPT0(0, "======================== FILE B =================================\n");
	for (auto& md : resultsB)
	{
		auto cur_module = dynamic_cast<CUG*>(app->Document()->uniqueIdDatabase.HandleToObjectWithNull(md.handle));

		std::wstring moduleName;
		if (cur_module)
		{
			moduleName = cur_module->GetName();
			if (moduleName.empty())
			{
				moduleName = cur_module->getType()->UniqueId();
			}
		}

		_RPT2(0, "0x08%x : %S\n", md.handle, moduleName.c_str());
		int index = 0;
		for (auto pd : md.pinData)
		{
			_RPT1(0, " %3d: ", index);
			for (int i = 0; i < 5; ++i)
			{
				_RPT1(0, "%f, ", pd[i]);
			}
			_RPT0(0, "\n");

			++index;
		}
		_RPT0(0, "\n");
	}
#endif

	// try to match-up modules with negative handles. 
//	_RPT0(0, "ORPHANS A\n");
//	for (const auto& r : resultsA)
//	{
//		if (r.handle < 0)
//		{
//			_RPT1(0, " %9d/%2 ->\n            ", r.handle, r.voice);
//			for (const auto& p : r.pins)
//			{
//				for (const auto& dest : p.toModuleAddress)
//				{
//					auto& [toHandle, toVoice] = lookupHandleFromAddress(dest, resultsA);
//
//					_RPT1(0, "%d, ", toHandle);
//				}
//				_RPT0(0, "\n");
//			}
////			_RPT0(0, "\n");
//		}
//	}
//	_RPT0(0, "ORPHANS B\n");
	// associate orphan with a signature.
	int tempHandle = -10000;
	int prevtempHandle = 0;
	while (tempHandle != prevtempHandle)
	{
		prevtempHandle = tempHandle;

		auto orphansA = getOrphans(resultsA);
		auto orphansB = getOrphans(resultsB);

		for (auto& a : orphansA)
		{
			for (auto& b : orphansB)
			{
				// if signatures match, assume it's the same module.
				if (b.second == a.second && a.first.voice == b.first.voice)
				{
					// replace old temp handle with new one.
					for (auto& m : resultsA)
					{
						if (m.handle == a.first.handle && m.voice == a.first.voice)
						{
							m.handle = tempHandle;
						}
					}
					for (auto& m : resultsB)
					{
						if (m.handle == b.first.handle && m.voice == b.first.voice)
						{
							m.handle = tempHandle;
						}
					}
					--tempHandle;
				}
			}
		}
	}

	//     handle/voice,  audio data
	std::map< moduleIdentity, cancelCompare > cancellationErrors;

	// merge the two snapshots
	for (auto& m : resultsA)
	{
		assert(cancellationErrors.find({ m.handle, m.voice }) == cancellationErrors.end()); // modules should be unique
		cancellationErrors[{m.handle, m.voice}].pinsA = &m.pins;
	}
	for (auto& m : resultsB)
	{
		cancellationErrors[{m.handle, m.voice}].pinsB = &m.pins;
	}

	// debug specific module
#if 0
	if (true)
	{
		const int moHandle = 1481061631;
		const int feeders[] = {
			875394319,
		};
		_RPT1(0, "module %d outputs : inputs\n", moHandle);

		auto d1 = cancellationErrors[{moHandle, 0}].samplesA;
		auto d2 = cancellationErrors[{moHandle, 0}].samplesB;

		for (size_t i = 0; i < blockSize; ++i)
		{
			_RPT2(0, "%f, %f :", d1[i], d2[1]);
			for (auto f : feeders)
			{
				auto d3 = cancellationErrors[{f, 0}].samplesA;
				_RPT1(0, ", %f\n", d3[i]);
			}
			_RPT0(0, "\n");
		}
		_RPT0(0, "\n");
	}
#endif

	{
		std::ostringstream myfile;
		{
			myfile << "File1: " << filenameA << std::endl;
			myfile << "File2: " << filenameB << std::endl;
			myfile << "Length: " << blockSize << std::endl;
			myfile << "Channels: " << 1 << std::endl << std::endl;
		}

//		_RPT1(0, "%s", myfile.str().c_str());
	}

	auto& sortedResults = app->Document()->cancellationResults;
	sortedResults.clear();

	std::map<moduleIdentity, inouterror> resultByModule;

	// calc error
	int errorWireCount = 0;
	for (auto& r : cancellationErrors)
	{
		if (!r.second.pinsA || !r.second.pinsB)
			continue;

		for (int i = 0; i < (std::min)(r.second.pinsA->size(), r.second.pinsB->size()); ++i)
		{
			auto pinA = r.second.pinsA->at(i);
			auto pinB = r.second.pinsB->at(i);

			if (pinA.audioData.empty() || pinB.audioData.empty())
				continue;

			int maxErrorTime = -1;
			float maxError = -200.0f;
			float sumError = 0.0f;
			const float* sourceSample = pinA.audioData.data();
			const float* destSample = pinB.audioData.data();

			for (size_t j = 0; j < blockSize; ++j)
			{
				float error = fabsf(*sourceSample - *destSample);
				error = (std::max)(error, 1e-10f);
				float errorDb = 20.0f * log10(error);
				sumError += errorDb;

				if (maxError < errorDb)
				{
					maxError = errorDb;
					maxErrorTime = static_cast<int>(i);
				}
				++sourceSample;
				++destSample;
			}

			float averageError = sumError / blockSize;

			if (maxErrorTime >= 0)
			{
				++errorWireCount;
				std::ostringstream myfile;
				{
					myfile
						<< r.first.handle << " : " << i
						<< " Max: " << maxError << " dB"
						<< " Avg: " << averageError << " dB"
						<< " Worst: smpl=" << maxErrorTime << std::endl;
				}
				//			_RPT1(0, "%s", myfile.str().c_str());
			}
			sortedResults.push_back({ r.first.handle, r.first.voice, i, maxError, averageError });

			// Store ouput pin error
			auto& myResult = resultByModule[r.first];
			myResult.outError = (std::max)(myResult.outError, maxError);

			// search for upstream module/pin, and copy error there to.
			for (auto dest : pinA.toModuleAddress)
			{
				auto [toHandle, toVoice] = lookupHandleFromAddress(dest, resultsA); // dest module
				auto& otherResult = resultByModule[{toHandle, toVoice}];
				otherResult.inError = (std::max)(otherResult.inError, maxError);
			}
		}
	}
//	_RPT1(0, "%d errors\n", errorWireCount);

	std::sort(sortedResults.begin(), sortedResults.end(), [](const cancelCompare2& n1, const cancelCompare2& n2) {
		return n1.errorMax > n2.errorMax;
	});
	/*
		_RPT0(0, "      handle       pin  max  avr\n");
		for (auto& r : sortedResults)
		{
			_RPT3(0, "0x%08x %2d %3.2f", r.handle, r.pin, r.errorMax);
			_RPT1(0, " %3.2f\n", r.errorAvr);
		}
	*/
	if (sortedResults.empty())
	{
//		_RPT0(0, "NO CANCELLATION ERROR\n");
		return;
	}

	std::vector<inouterror> candidatesSorted;
	for (const auto& c : resultByModule)
	{
		candidatesSorted.push_back({ c.first, c.second.inError, c.second.outError });
	}

//	_RPT0(0, "\n===============================\nTop cancellation error modules\n");
	std::sort(candidatesSorted.begin(), candidatesSorted.end(), [](const inouterror& n1, const inouterror& n2) {

		const auto n1IsTempModule = n1.moduleId.handle < 0;
		const auto n2IsTempModule = n2.moduleId.handle < 0;
		if (n1IsTempModule != n2IsTempModule)
		{
			return n2IsTempModule;
		}

		float magnitude1 = n1.outError - n1.inError;
		float magnitude2 = n2.outError - n2.inError;

		if (n1.inError == -300.f) // -300 = don't know, sort below known errors
			magnitude1 = n1.outError - -199.f;

		if (n2.inError == -300.f) // -300 = don't know, sort below known errors
			magnitude2 = n2.outError - -199.f;

		return magnitude1 > magnitude2;
	});

	int rank = 0;
	for (const auto& c : candidatesSorted)
	{
		const float noErrorCancellation = -200.f;

		if (c.inError <= noErrorCancellation && c.outError <= noErrorCancellation)
			continue;

		if (noErrorCancellation == c.outError)
			continue;

		auto cur_module = dynamic_cast<CUG*>(app->Document()->uniqueIdDatabase.HandleToObjectWithNull(c.moduleId.handle));

		std::wstring moduleName;
		if (cur_module)
		{
			moduleName = cur_module->GetName();
			if (moduleName.empty())
			{
				moduleName = cur_module->getType()->UniqueId();
			}
		}
#ifdef _WIN32
		_RPTN(0, "V%2d %12d (0x%08x) ", c.moduleId.voice, c.moduleId.handle, c.moduleId.handle);
		if (c.inError == -300.f)
		{
			_RPT0(0, "in: ???     ");
		}
		else if (c.inError == -200.f)
		{
			_RPT0(0, "in: <inf>   ");
		}
		else
		{
			_RPT1(0, "in:%7.2f  ", c.inError);
		}

		if (c.outError == -300.f)
		{
			_RPT0(0, "out: ???     ");
		}
		else if (c.outError == -200.f)
		{
			_RPT0(0, "out: <inf>   ");
		}
		else
		{
			_RPT1(0, "out:%7.2f\t", c.outError);
		}
		_RPT1(0, "%S\n", moduleName.c_str());
#endif
        
		// Print pin samples
		if (rank < 10 || 1481061631 == c.moduleId.handle) /// !!! you can put a particular module handle here for a deeper printout !!!
		{
			// inputs
//			_RPT0(0, "INPUTS\n");
			{
				for (auto& r : cancellationErrors)
				{
					auto pinsA = r.second.pinsA;
					if (pinsA)
					{
						for (const auto& pin : *pinsA)
						{
							for (const auto& toModuleAddress : pin.toModuleAddress)
							{
								const auto toModuleId = lookupHandleFromAddress2(toModuleAddress, resultsA);
								if (toModuleId == c.moduleId)
								{
									printAudioData('A', pin.audioData);
								}
							}
						}
					}
				}
				for (auto& r : cancellationErrors)
				{
					auto pinsB = r.second.pinsB;
					if (pinsB)
					{
						for (const auto& pin : *pinsB)
						{
							for (const auto& toModuleAddress : pin.toModuleAddress)
							{
								const auto toModuleId = lookupHandleFromAddress2(toModuleAddress, resultsB);
								if (toModuleId == c.moduleId)
								{
									printAudioData('B', pin.audioData);
								}
							}
						}
					}
				}
			}

			// outputs
//			_RPT0(0, "OUTPUTS\n");
			for (auto& r : cancellationErrors)
			{
				if (r.first != c.moduleId)
					continue;
				if (!r.second.pinsA || !r.second.pinsB)
					continue;

				for (int i = 0; i < (std::min)(r.second.pinsA->size(), r.second.pinsB->size()); ++i)
				{
					auto pinA = r.second.pinsA->at(i);
					auto pinB = r.second.pinsB->at(i);

					if (pinA.audioData.empty() || pinB.audioData.empty())
						continue;

					printAudioData('A', pinA.audioData);
					printAudioData('B', pinB.audioData);
				}
			}
//			_RPT0(0, "\n");
		}

#if 0
		// is input module possibly lost?
		if (c.inError == -200.0f)
		{
			// find predecessors.
			{
				std::vector<int32_t> feeders;
				for (const auto& ra : resultsA)
				{
					for (auto toModule : ra.toModuleAddress)
					{
						const auto [handle, voice] = lookupHandleFromAddress(toModule, resultsA);
						if (handle == c.identity.handle && voice == c.identity.voice)
						{
							feeders.push_back(ra.handle);
						}
					}
				}
				if (!feeders.empty())
				{
					std::sort(feeders.begin(), feeders.end());
					feeders.erase(std::unique(feeders.begin(), feeders.end()), feeders.end());

					_RPT0(0, "                      snap A feeders\n");
					for (auto f : feeders)
					{
						_RPT1(0, "                                 %d\n", f);
					}
				}
			}
			{
				std::vector<int32_t> feeders;
				for (const auto& ra : resultsB)
				{
					for (auto toModule : ra.toModuleAddress)
					{
						const auto [handle, voice] = lookupHandleFromAddress(toModule, resultsB);
						if (handle == c.identity.handle && voice == c.identity.voice)
						{
							feeders.push_back(ra.handle);
						}
					}
				}
				if (!feeders.empty())
				{
					std::sort(feeders.begin(), feeders.end());
					feeders.erase(std::unique(feeders.begin(), feeders.end()), feeders.end());

					_RPT0(0, "                      snap B feeders\n");
					for (auto f : feeders)
					{
						_RPT1(0, "                                 %d\n", f);
					}
				}
			}
		}
#endif
		++rank;
	}

//	_RPT0(0, "\n===============================\n");

#endif
}
