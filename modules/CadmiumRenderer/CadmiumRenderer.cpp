
/* Copyright (c) 2007-2021 SynthEdit Ltd
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name SEM, nor SynthEdit, nor 'Music Plugin Interface' nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY SynthEdit Ltd ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL SynthEdit Ltd BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "helpers/GmpiPluginEditor.h"
#include "jsoncpp/json/json.h"
#include "Cadmium/GUI_3_0.h"
#include "helpers/Timer.h"

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

struct moduleConnection
{
	int32_t toModuleHandle = {};
	int32_t fromModulePin = {};
	int32_t toModulePin = {};
};

struct moduleInfo
{
	int32_t sort = -1; // unsorted
	std::vector<moduleConnection> connections;
};

void CalcSortOrder2(std::map<int32_t, moduleInfo>& allModules, moduleInfo& m, int maxSortOrderGlobal)
{
	m.sort = -2; // prevent recursion back to this.
	int maxSort = -1; // reset.
	for (auto connector : m.connections)
	{
		assert(allModules.find(connector.toModuleHandle) != allModules.end());
		auto& to = allModules[connector.toModuleHandle];
		{
			{
				int order = to.sort;
				if (order == -2) // Found an feedback path, report it.
				{
					assert(false); // FEEDBACK!
					return;
				}

				if (order == -1) // Found an unsorted path, go down it.
				{
					CalcSortOrder2(allModules, to, maxSortOrderGlobal);
					{
						order = to.sort;
					}
				}

				maxSort = (std::max)(maxSort, order);
			}
		}
	}

	++maxSort;

	assert(maxSort > -1);

	m.sort = maxSort;
	maxSortOrderGlobal = (std::max)(maxSort, maxSortOrderGlobal);
}

// ref ug_container::SortOrderSetup()
void SortModules(std::map<int32_t, moduleInfo>& allModules)
{
	int maxSortOrderGlobal = -1;

	for (auto& it : allModules)
	{
		auto& m = it.second;

		if (m.sort >= 0) // Skip sorted modules.
		{
			continue;
		}

		CalcSortOrder2(allModules, m, maxSortOrderGlobal);
	}
}

class CadmiumRenderer final : public PluginEditor, public TimerClient
{
	Pin<std::string> pinJson;

	functionalUI functionalUI;
	std::vector<::node*> renderNodes2;

	void update()
	{
		// de-serialize JSON into object graph.
		Json::Value document_json;
		{
			Json::Reader reader;
			const auto jsonString = pinJson.value;
			reader.parse(jsonString, document_json);
		}

		// sort modules (actually a 'dumb' representiative of the module)
		std::map<int32_t, moduleInfo> moduleSort;
		for (auto& module_json : document_json["connections"])
		{
			const auto fromHandle = module_json["fMod"].asInt();
			const auto toHandle = module_json["tMod"].asInt();
			const auto fromPin = module_json["fPin"].asInt();
			const auto toPin = module_json["tPin"].asInt();

			moduleSort[fromHandle].connections.push_back(
				{
					toHandle,
					fromPin,
					toPin
				}
			);

			auto unused = moduleSort[toHandle].sort; // ensure we have a record for all 'to' modules.
		}

		SortModules(moduleSort);

		std::vector<int32_t> renderNodeHandles;

		for (auto& module_json : document_json["modules"])
		{
			const auto typeName = module_json["type"].asString();
			const auto handle = module_json["handle"].asInt();

			auto it = factory.find(typeName);
			if (it != factory.end())
			{
				auto& createNode = it->second;

				createNode(handle, module_json, functionalUI.states2, functionalUI.graph);
			}

			if ("SE Render" == typeName)
			{
				renderNodeHandles.push_back(handle);
			}
		}

		// sort actual nodes in line with 'moduleSort' structure.
		std::sort(
			functionalUI.graph.begin(),
			functionalUI.graph.end(),
			[&moduleSort](const ::node& n1, const ::node& n2)
				{
					return moduleSort[n1.handle].sort > moduleSort[n2.handle].sort;
				}
			);


		std::unordered_map<int32_t, size_t> handleToIndex;
		for (int index = 0; index < functionalUI.graph.size(); ++index)
		{
			handleToIndex.insert({ functionalUI.graph[index].handle, index });
		}

		for (auto handle : renderNodeHandles)
		{
			const auto index = handleToIndex[handle];
			renderNodes2.push_back(&functionalUI.graph[index]);
		}

		for (auto& module_json : document_json["connections"])
		{
			const auto fromHandle = module_json["fMod"].asInt();
			const auto toHandle = module_json["tMod"].asInt();
			const auto fromPin = module_json["fPin"].asInt();
			const auto toPin = module_json["tPin"].asInt();

			std::vector<state_t*>* destArguments = {};
			if (auto it = handleToIndex.find(toHandle); it != handleToIndex.end())
			{
				const auto toNodeIndex = (*it).second;
				destArguments = &functionalUI.graph[toNodeIndex].arguments;

				// pad any inputs that have not been connected yet.
				while (destArguments->size() <= toPin)
				{
					destArguments->push_back({});
				}

				const auto fromNodeIndex = handleToIndex[fromHandle];
				(*destArguments)[toPin] = &functionalUI.graph[fromNodeIndex].result;
			}
		}

		functionalUI.step();
	}

	bool onTimer() override
	{
		functionalUI.step();

		if (drawingHost)
		{
			Rect clipArea;
			getClipArea(&clipArea);
			drawingHost->invalidateRect(&clipArea);
		}

		return true;
	}

	std::unordered_map<std::string, std::function<void(int32_t, Json::Value&, std::vector< std::unique_ptr<observableState> >&, std::vector<::node>&)> > factory;

public:
	CadmiumRenderer()
	{
		pinJson.onUpdate = [this](PinBase*) { update(); };

		// Init factory
		factory.insert
		(
			{ "SE Solid Color Brush",
			[](int32_t handle, Json::Value& module_json, std::vector< std::unique_ptr<observableState> >& states, std::vector<::node>& graph)
			{
				Color brushColor = Colors::Black;

				ScanPinDefaults(module_json,
					[&brushColor](int idx, const std::string& value)
				{
					if (idx == 0)
					{
						brushColor = colorFromHexString(value);
					}
				}
				);

				// Add state for input value (color)
				states.push_back(
					std::make_unique<observableState>(brushColor)
				);

				// Add function
				graph.push_back(
					{
						[](std::vector<state_t*> statesx) -> state_data_t
						{
							return vBrush(std::get<Color>(statesx[0]->value));
						},
						{states.back().get()},
						{ 0.0f },
						handle
					}
				);
			}
			});

		//////////////////////////////////////////////////////////////////
		factory.insert
		(
			{ "CD Circle",
		[](int32_t handle, Json::Value& module_json, std::vector< std::unique_ptr<observableState> >& states, std::vector<::node>& graph)
		{
			float radius{ 10.f };
			Point center{ 100.0f, 100.0f };

			ScanPinDefaults(module_json,
				[&radius](int idx, const std::string& value)
				{
					if (idx == 0)
					{
						radius = std::stof(value);
					}
				}
			);

			// center
			const auto input1Idx = states.size();
			states.push_back(
				std::make_unique<observableState>(center)
			);

			// radius
			const auto input2Idx = states.size();
			states.push_back(
				std::make_unique<observableState>(radius)
			);

			// 2. circle geometry
			graph.push_back(
				{
				[](std::vector<state_t*> states) -> state_data_t
					{
						const auto& center = std::get<Point>(states[0]->value);
						const auto& radius = std::get<float>(states[1]->value);

						return vCircleGeometry(center, radius);
					},
					{
						states[input1Idx].get(),
						states[input2Idx].get()
					},
					{0.0f},
					handle
				}
			);
		}
			}
		);
		//////////////////////////////////////////////////////////////////
		factory.insert
		(
			{ "CD Square",
		[](int32_t handle, Json::Value& module_json, std::vector< std::unique_ptr<observableState> >& states, std::vector<::node>& graph)
		{
			float size{ 10.f };
			Point center{ 100.0f, 100.0f };

			ScanPinDefaults(module_json,
				[&size](int idx, const std::string& value)
				{
					if (idx == 0)
					{
						size = std::stof(value);
					}
				}
			);

			// center
			const auto input1Idx = states.size();
			states.push_back(
				std::make_unique<observableState>(center)
			);

			const auto input2Idx = states.size();
			states.push_back(
				std::make_unique<observableState>(size)
			);

			graph.push_back(
				{
				[](std::vector<state_t*> states) -> state_data_t
					{
						const auto& center = std::get<Point>(states[0]->value);
						const auto& size = std::get<float>(states[1]->value);

						return vSquareGeometry(center, size);
					},
					{
						states[input1Idx].get(),
						states[input2Idx].get()
					},
					{0.0f},
					handle
				}
			);
		}
			}
		);

		//////////////////////////////////////////////////////////////////
		factory.insert
		(
			{ "SE Render",
		[](int32_t handle, Json::Value& module_json, std::vector< std::unique_ptr<observableState> >& states, std::vector<::node>& graph)
		{
			// 0. render
			graph.push_back(
				{
					[](std::vector<state_t*> states) -> state_data_t
					{
						auto brush = std::get_if<vBrush>(&states[0]->value);

						vGeometry* geometry = {};
						if(states.size() > 1)
						{
							geometry = std::get_if<vCircleGeometry>(&states[1]->value);
							if (!geometry)
							{
								geometry = std::get_if<vSquareGeometry>(&states[1]->value);
							}
						}

						return RendererX(
						{[brush, geometry](Graphics& g) -> void
						{
							if (geometry && brush)
							{
								g.fillGeometry(geometry->native(g), brush->native(g));
							}
						}});
					},
					{},
					{0.0f},
					handle
				}
			);

			}
			}
		);
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);

		for (auto& rendernode : renderNodes2)
		{
			std::get<RendererX>(rendernode->result.value).function(g);
		}

		return ReturnCode::Ok;
	}

	ReturnCode onPointerMove(Point point, int32_t flags) override
	{
		functionalUI.onPointerMove(flags, point);
		return ReturnCode::Ok;
	}
};

namespace
{
	auto r = gmpi::Register<CadmiumRenderer>::withId("SE CadmiumRenderer");
}
