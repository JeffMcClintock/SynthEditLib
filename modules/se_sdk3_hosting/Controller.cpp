#include <codecvt>
#include <locale>
#include <thread>
#include <fstream>
#include <filesystem>
#include "Controller.h"
#include "../tinyXml2/tinyxml2.h"
#include "RawConversions.h"
#include "HostControls.h"
#include "GmpiResourceManager.h"
#include "./Presenter.h"
#include "BundleInfo.h"
#include "FileFinder.h"
#include "midi_defs.h"
#include "ListBuilder.h"
#include "../../mfc_emulation.h"
#if !defined(SE_USE_JUCE_UI)
#include "GuiPatchAutomator3.h"
#endif
#ifndef GMPI_VST3_WRAPPER
#include "../../UgDatabase.h"
#include "PresetReader.h"
#endif

#if 0
#include "../shared/unicode_conversion.h"
#include "../../UgDatabase.h"
#include "../../modules/shared/string_utilities.h"
#include "../shared/unicode_conversion.h"
#include "PresetReader.h"
#include "./ProcessorStateManager.h"

#endif
#include "conversion.h"
#include "UniqueSnowflake.h"

#ifdef _DEBUG
// #define DEBUG_UNDO
#endif

using namespace std;

MpController::~MpController()
{
    if(presenter_)
    {
        presenter_->OnControllerDeleted();
    }
}

std::pair<bool, bool> MpController::CategorisePresetName(const std::string& newName)
{
	bool isReadOnly = false;
	bool isExistingName = false;

	assert(!full_reset_preset_name.empty()); // you gotta set this from teh default preset.
	if (newName == full_reset_preset_name)
	{
		isReadOnly = isExistingName = true;
	}

	for (int i = 0; i < getPresetCount(); ++i)
	{
		auto preset = getPresetInfo(i);
		if (preset.isSession)
			continue;

		if (preset.name == newName)
		{
			isReadOnly = preset.isFactory;
			isExistingName = true;
			break;
		}
	}

	return { isReadOnly, isExistingName };
}

void MpController::ScanPresets()
{
	assert(this->isInitialized);

	presets.clear(); // fix crash on JUCE
	presets = scanNativePresets(); // Scan VST3 presets (both VST2 and VST3)

	// Factory presets from bundles presets folder.
	{
		auto presets2 = scanFactoryPresets();

		// skip duplicates of disk presets (because VST3 plugin will have them all on disk, and VST2 will scan same folder.
		const auto nativePresetsCount = presets.size();
		for (auto& preset : presets2)
		{
			// Is this a duplicate?
			bool isDuplicate = false;
			for (size_t i = 0; i < nativePresetsCount; ++i)
			{
				if (presets[i].name == preset.name)
				{
					// preset occurs in VST presets folder and ALSO in preset XML.
					isDuplicate = true;
					//presets[i].index = preset.index; // Don't insert it twice, but note that it is an internal preset. (VST3 Preset will be ignored).
					presets[i].isFactory = true;
					break;
				}
			}

			if (!isDuplicate)
			{
				preset.isFactory = true;
				presets.push_back({ preset });
			}
		}
	}
	{
#if 0
	// Factory presets from factory.xmlpreset resource.
		auto nativePresetsCount = presets.size();

		// Harvest factory preset names.
		auto factoryPresetFolder = ToPlatformString(BundleInfo::instance()->getImbedded FileFolder());
		string filenameUtf8 = ToUtf8String(factoryPresetFolder) + "factory.xmlpreset";

		TiXmlDocument doc;
		doc.LoadFile(filenameUtf8);

		if (!doc.Error()) // if file does not exist, that's OK. Only means we're a VST3 plugin and don't have internal presets.
		{
			TiXmlHandle hDoc(&doc);
			TiXmlElement* pElem;
			{
				pElem = hDoc.FirstChildElement().Element();

				// should always have a valid root but handle gracefully if it does not.
				if (!pElem)
					return;
			}

			const char* pKey = pElem->Value();
			assert(strcmp(pKey, "Presets") == 0);

			int i = 0;
			for (auto preset_xml = pElem->FirstChildElement("Preset"); preset_xml; preset_xml = preset_xml->NextSiblingElement())
			{
				presetInfo preset;
				preset.index = i++;

				preset_xml->QueryStringAttribute("name", &preset.name);
				preset_xml->QueryStringAttribute("category", &preset.category);

				// skip duplicates of disk presets (because VST3 plugin will have them all on disk, and VST2 will scan same folder.
				// Is this a duplicate?
				bool isDuplicate = false;
				for (size_t i = 0; i < nativePresetsCount; ++i)
				{
					if (presets[i].name == preset.name)
					{
						// preset occurs in VST presets folder and ALSO in preset XML.
						isDuplicate = true;
						presets[i].index = preset.index; // Don't insert it twice, but note that it is an internal preset. (VST3 Preset will be ignored).
						break;
					}
				}

				if (!isDuplicate)
				{
					presets.push_back(preset);
				}
			}
		}
#endif
		// sort all presets by category/index.
		std::sort(presets.begin(), presets.end(),
			[=](const presetInfo& a, const presetInfo& b) -> bool
			{
				// Sort by category
				if (a.category != b.category)
				{
					// blank category last
					if (a.category.empty() != b.category.empty())
						return a.category.empty() < b.category.empty();

					return a.category < b.category;
				}

				// ..then by index
				if (a.index != b.index)
					return a.index < b.index;

				return a.name < b.name;
			});
	}

#ifdef _DEBUG
	for (auto& preset : presets)
	{
		assert(!preset.filename.empty() || preset.isSession || preset.isFactory);
	}
#endif
}

void MpController::UpdatePresetBrowser()
{
	// Update preset browser indirectly by updating teh relevant host-controls
	for (auto& p : parameters_)
	{
		if (p->getHostControl() == HC_PROGRAM_CATEGORIES_LIST || p->getHostControl() == HC_PROGRAM_NAMES_LIST)
		{
			UpdateProgramCategoriesHc(p.get());
			updateGuis(p.get(), gmpi::FieldType::MP_FT_VALUE);
		}
	}
}

void MpController::Initialize()
{
    if(isInitialized)
    {
        return; // Prevent double-up on parameters.
    }

	// ensure resource manager knows where to find things
	{
		auto& resourceFolders = GmpiResourceManager::Instance()->resourceFolders;
		const auto pluginResourceFolder = BundleInfo::instance()->getResourceFolder();

		resourceFolders[GmpiResourceType::Midi] = pluginResourceFolder;
		resourceFolders[GmpiResourceType::Image] = pluginResourceFolder;
		resourceFolders[GmpiResourceType::Audio] = pluginResourceFolder;
		resourceFolders[GmpiResourceType::Soundfont] = pluginResourceFolder;
	}
#ifndef GMPI_VST3_WRAPPER

	// Ensure we can access SEM Controllers info
	ModuleFactory()->RegisterExternalPluginsXmlOnce(nullptr);

//	TiXmlDocument doc;
	tinyxml2::XMLDocument doc;
	{
		const auto xml = BundleInfo::instance()->getResource("parameters.se.xml");
		doc.Parse(xml.c_str());
		assert(!doc.Error());
	}

	auto controllerE = doc.FirstChildElement("Controller");
	assert(controllerE);

	auto patchManagerE = controllerE->FirstChildElement();
	assert(strcmp(patchManagerE->Value(), "PatchManager") == 0);

	std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;

	auto parameters_xml = patchManagerE->FirstChildElement("Parameters");

	::init(parametersInfo, parameters_xml); // for parsing presets

	for (auto parameter_xml = parameters_xml->FirstChildElement("Parameter"); parameter_xml; parameter_xml = parameter_xml->NextSiblingElement("Parameter"))
	{
		int dataType = DT_FLOAT;
		int ParameterTag = -1;
		int ParameterHandle = -1;
		int Private = 0;

		std::string Name = parameter_xml->Attribute("Name");
		parameter_xml->QueryIntAttribute("ValueType", &dataType);
		parameter_xml->QueryIntAttribute("Index", &ParameterTag);
		parameter_xml->QueryIntAttribute("Handle", &ParameterHandle);
		parameter_xml->QueryIntAttribute("Private", &Private);

		if (dataType == DT_TEXT || dataType == DT_BLOB)
		{
			Private = 1; // VST and AU can't handle this type of parameter.
		}
		else
		{
			if (Private != 0)
			{
				// Check parameter is numeric and a valid type.
				assert(dataType == DT_ENUM || dataType == DT_DOUBLE || dataType == DT_BOOL || dataType == DT_FLOAT || dataType == DT_INT || dataType == DT_INT64);
			}
		}

		int stateful_ = 1;
		parameter_xml->QueryIntAttribute("persistant", &stateful_);
		int hostControl = -1;
		parameter_xml->QueryIntAttribute("HostControl", &hostControl);
		int ignorePc = 0;
		parameter_xml->QueryIntAttribute("ignoreProgramChange", &ignorePc);

		double pminimum = 0.0;
		double pmaximum = 10.0;

		parameter_xml->QueryDoubleAttribute("RangeMinimum", &pminimum);
		parameter_xml->QueryDoubleAttribute("RangeMaximum", &pmaximum);

		int moduleHandle_ = -1;
		int moduleParamId_ = 0;
		bool isPolyphonic_ = false;
		wstring enumList_;

		parameter_xml->QueryIntAttribute("Module", &(moduleHandle_));
		parameter_xml->QueryIntAttribute("ModuleParamId", &(moduleParamId_));
		parameter_xml->QueryBoolAttribute("isPolyphonic", &(isPolyphonic_));

		if (dataType == DT_INT || dataType == DT_TEXT /*|| dataType == DT_ENUM */)
		{
			auto s = parameter_xml->Attribute("MetaData");
			if (s)
				enumList_ = convert.from_bytes(s);
		}

		MpParameter_base* seParameter = nullptr;

		if (Private == 0)
		{
			assert(ParameterTag >= 0);
			seParameter = makeNativeParameter(ParameterTag, pminimum > pmaximum);
		}
		else
		{
			auto param = new MpParameter_private(this);
			seParameter = param;
			param->isPolyphonic_ = isPolyphonic_;
		}

		seParameter->hostControl_ = hostControl;
		seParameter->minimum = pminimum;
		seParameter->maximum = pmaximum;

		parameter_xml->QueryIntAttribute("MIDI", &(seParameter->MidiAutomation));
		if (seParameter->MidiAutomation != -1)
		{
			const char* temp{};
			parameter_xml->QueryStringAttribute("MIDI_SYSEX", &temp);
			seParameter->MidiAutomationSysex = Utf8ToWstring(temp);
		}

		// Preset values from patch list.
		ParseXmlPreset(
			parameter_xml,
			[seParameter, dataType](int voiceId, int preset, const char* xmlvalue)
			{
				seParameter->rawValues_.push_back(ParseToRaw(dataType, xmlvalue));
			}
		);

		// no patch-list?, init to zero.
		if (!parameter_xml->FirstChildElement("patch-list"))
		{
			assert(!stateful_);

			// Special case HC_VOICE_PITCH needs to be initialized to standard western scale
			if (HC_VOICE_PITCH == hostControl)
			{
				const int middleA = 69;
				constexpr float invNotesPerOctave = 1.0f / 12.0f;
				seParameter->rawValues_.reserve(128);
				for (float key = 0; key < 128; ++key)
				{
					const float pitch = 5.0f + static_cast<float>(key - middleA) * invNotesPerOctave;
					std::string raw((const char*) &pitch, sizeof(pitch));
					seParameter->rawValues_.push_back(raw);
				}
			}
			else
			{
				// init to zero
				const char* nothing = "\0\0\0\0\0\0\0\0";
				std::string raw(nothing, getDataTypeSize(dataType));
				seParameter->rawValues_.push_back(raw);
			}
		}

		seParameter->parameterHandle_ = ParameterHandle;
		seParameter->datatype_ = dataType;
		seParameter->moduleHandle_ = moduleHandle_;
		seParameter->moduleParamId_ = moduleParamId_;
		seParameter->stateful_ = stateful_;
		seParameter->name_ = convert.from_bytes(Name);
		seParameter->enumList_ = enumList_;
		seParameter->ignorePc_ = ignorePc != 0;

		parameters_.push_back(std::unique_ptr<MpParameter>(seParameter));
		ParameterHandleIndex.insert({ ParameterHandle, seParameter });
		moduleParameterIndex.insert({ {moduleHandle_, moduleParamId_}, ParameterHandle });
        
        // Ensure host queries return correct value.
        seParameter->upDateImmediateValue();

		if (seParameter->hostControl_ == HC_PROGRAM_NAME)
		{
			full_reset_preset_name = WStringToUtf8(RawToValue<std::wstring>(seParameter->rawValues_[0].data(), seParameter->rawValues_[0].size()));
		}
	}

	// SEM Controllers.
	{
		assert(controllerE);

		auto childPluginsE = controllerE->FirstChildElement("ChildControllers");
		for (auto childE = childPluginsE->FirstChildElement("ChildController"); childE; childE = childE->NextSiblingElement("ChildController"))
		{
			std::string typeId = childE->Attribute("Type");

			auto mi = ModuleFactory()->GetById(Utf8ToWstring(typeId));

			if (!mi)
			{
				continue;
			}

			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> obj;
			obj.Attach(mi->Build(gmpi::MP_SUB_TYPE_CONTROLLER, true));

			if (obj)
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpController> controller;
				/*auto r = */ obj->queryInterface(gmpi::MP_IID_CONTROLLER, controller.asIMpUnknownPtr());

				if (controller)
				{
					int32_t handle = 0;
					childE->QueryIntAttribute("Handle", &(handle));
					semControllers.addController(handle, controller);

					// Duplicating all the pins and defaults seems a bit redundant, they may not even be needed.
					// Perhaps controller needs own dedicated pins????

					// Create IO and autoduplicating Plugs. Set defaults.
					auto plugsElement = childE->FirstChildElement("Pins");

					if (plugsElement)
					{
						int32_t i = 0;
						for (auto plugElement = plugsElement->FirstChildElement(); plugElement; plugElement = plugElement->NextSiblingElement())
						{
							assert(strcmp(plugElement->Value(), "Pin") == 0);

							plugElement->QueryIntAttribute("idx", &i);
							int32_t pinType = 0;
							plugElement->QueryIntAttribute("type", &pinType);
							auto d = plugElement->Attribute("default");

							if (!d)
								d = "";

							controller->setPinDefault(pinType, i, d);

							++i;
						}
					}
				}
			}
		}
	}
#endif

	// crashes in JUCE VST3 plugin helper: EXEC : error : FindFirstChangeNotification function failed. [D:\a\1\s\build\Optimus\Optimus_VST3.vcxproj]
#if (GMPI_IS_PLATFORM_JUCE==0)
	{
		auto presetFolderPath = toPlatformString(BundleInfo::instance()->getPresetFolder());
		if (!presetFolderPath.empty())
		{
			fileWatcher.Start(
				presetFolderPath,
				[this]()
				{
					// note: called from background thread.
					presetsFolderChanged = true;
				}
			);
		}
	}
#endif
    
	undoManager.initial(this, getPreset());

    isInitialized = true;
}

void MpController::initSemControllers()
{
	if (!isSemControllersInitialised)
	{
		//		_RPT0(_CRT_WARN, "ADelayController::initSemControllers\n");

		for (auto& cp : semControllers.childPluginControllers)
		{
			cp.second->controller_->open();
		}

		isSemControllersInitialised = true;
	}
}

int32_t MpController::getController(int32_t moduleHandle, gmpi::IMpController** returnController)
{
	for (auto& m : semControllers.childPluginControllers)
	{
		if (m.first == moduleHandle)
		{
			*returnController = m.second->controller_;
			break;
		}
	}

	return gmpi::MP_OK;
}

std::vector< MpController::presetInfo > MpController::scanNativePresets()
{
	platform_string PresetFolder = toPlatformString(BundleInfo::instance()->getPresetFolder());

	auto extension = ToPlatformString(getNativePresetExtension());

	return scanPresetFolder(PresetFolder, extension);
}

MpController::presetInfo MpController::parsePreset(const std::wstring& filename, const std::string& xml)
{
	// file name overrides the name from XML
	std::string presetName;
	{
		std::wstring shortName, path_unused, extension;
		decompose_filename(filename, shortName, path_unused, extension);

		presetName = JmUnicodeConversions::WStringToUtf8(shortName);

		// Remove preset number prefix if present. "0023_Sax" -> "Sax"
		if (presetName.size() > 6
			&& presetName[4] == '_'
			&& isdigit(presetName[0])
			&& isdigit(presetName[1])
			&& isdigit(presetName[2])
			&& isdigit(presetName[3])
			)
		{
			presetName = presetName.substr(5);
		}
	}

	// load preset into a temporary object to get hash.
	auto preset = std::make_unique<DawPreset>(parametersInfo, xml);

	if (preset->name != presetName && !presetName.empty())
	{
		preset->name = presetName;

		// recalc hash with new name
		preset->calcHash();
	}

	return
	{
		preset->name,
		preset->category,
		-1,
		filename,
		preset->hash,
		false, // isFactory
		false  // isSession
	};
	}

std::vector< MpController::presetInfo > MpController::scanPresetFolder(platform_string PresetFolder, platform_string extension)
{
	std::vector< presetInfo > returnValues;

	const auto searchString = PresetFolder + platform_string(_T("*.")) + extension;
	const bool isXmlPreset = ToUtf8String(extension) == "xmlpreset";

	FileFinder it(searchString.c_str());
	for (; !it.done(); ++it)
	{
		if (!(*it).isFolder)
		{
			const auto sourceFilename = (*it).fullPath;

			std::string xml;
			if (isXmlPreset)
			{
				FileToString(sourceFilename, xml);
            }
			else
			{
                xml = loadNativePreset(ToWstring(sourceFilename));
			}

			if (!xml.empty())
			{
				const auto preset = parsePreset(ToWstring(sourceFilename), xml);
				if(!preset.filename.empty()) // avoid ones that fail to parse
				{
					returnValues.push_back(preset);
				}
			}
		}
	}

	return returnValues;
}

void MpController::setParameterValue(RawView value, int32_t parameterHandle, gmpi::FieldType paramField, int32_t voice)
{
	auto it = ParameterHandleIndex.find(parameterHandle);
	if (it == ParameterHandleIndex.end())
	{
		return;
	}

	auto seParameter = (*it).second;

	bool takeUndoSnapshot = false;

	// Special case for MIDI Learn
	if (paramField == gmpi::MP_FT_MENU_SELECTION)
	{
		auto choice = (int32_t)value;// RawToValue<int32_t>(value.data(), value.size());

			// 0 not used as it gets passed erroneously during init
		int cc = 0;
		if (choice == 1) // learn
		{
			cc = ControllerType::Learn;
		}
		else
		{
			if (choice == 2) // un-learn
			{
				cc = ControllerType::None;

				// set automation on GUI to 'none'
				seParameter->MidiAutomation = cc;
				updateGuis(seParameter, gmpi::MP_FT_AUTOMATION);
			}
		}

		// Send MIDI learn message to DSP.
		//---send a binary message
		if (cc != 0)
		{
			my_msg_que_output_stream s(getQueueToDsp(), parameterHandle, "CCID");

			s << (int)sizeof(int);
			s << cc;
			s.Send();
		}
	}
	else
	{
		if (seParameter->setParameterRaw(paramField, value.size(), value.data(), voice))
		{
			seParameter->updateProcessor(paramField, voice);

			if (seParameter->stateful_ && (paramField == gmpi::MP_FT_VALUE|| paramField == gmpi::MP_FT_NORMALIZED))
			{
				if (!seParameter->isGrabbed()) // e.g. momentary button
				{
					takeUndoSnapshot = true;
				}
			}
		}
	}

	// take an undo snapshot anytime a knob is released
	if (paramField == gmpi::MP_FT_GRAB)
	{
		const bool grabbed = (bool)value;
		if (!grabbed && seParameter->stateful_)
		{
			takeUndoSnapshot = true;
		}
	}

	if (takeUndoSnapshot)
	{
		setModified(true);

		const auto paramName = WStringToUtf8((std::wstring)seParameter->getValueRaw(gmpi::MP_FT_SHORT_NAME, 0));

		const std::string desc = "Changed parameter: " + paramName;
		undoManager.snapshot(this, desc);
	}
}

void UndoManager::debug()
{
#ifdef DEBUG_UNDO
	_RPT0(0, "\n======UNDO=======\n");
	for (int i = 0 ; i < size() ; ++i)
	{
		_RPT1(0, "%c", i == undoPosition ? '>' : ' ');
		_RPTN(0, "%s\n", history[i].first.empty() ? "<init>" : history[i].first.c_str());
	}
	_RPTN(0, "CAN UNDO %d\n", (int)canUndo());
	_RPTN(0, "CAN REDO %d\n", (int)canRedo());
#endif
}

void UndoManager::setPreset(MpController* controller, DawPreset const* preset)
{
//	controller->dawStateManager.setPreset(preset);
	controller->setPresetFromSelf(preset);

#ifdef DEBUG_UNDO
	_RPT0(0, "UndoManager::setPreset\n");
	debug();
#endif
}

void UndoManager::initial(MpController* controller, std::unique_ptr<const DawPreset> preset)
{
	history.clear();
	push({}, std::move(preset));

	UpdateGui(controller);

#ifdef DEBUG_UNDO
	_RPT0(0, "UndoManager::initial (2)\n");
	debug();
#endif
}

bool UndoManager::canUndo()
{
	return undoPosition > 0 && undoPosition < size();
}

bool UndoManager::canRedo()
{
	return undoPosition >= 0 && undoPosition < size() - 1;
}

bool UndoManager::isPresetModified()
{
	if(!canUndo())
		return false;

#if 0
	auto as = history[0].second->toString(0);
	auto bs = history[undoPosition].second->toString(0);

	as = as.substr(0, 200);
	bs = bs.substr(0, 200);
	_RPTN(0, "--Orig --\n%s\nhash %d\n", as.c_str(), (int)history[0].second->hash);
	_RPTN(0, "--head --\n%s\nhash %d\n", bs.c_str(), (int)history[undoPosition].second->hash);
#endif

	return history[undoPosition].second->hash != history[0].second->hash;
}

void UndoManager::UpdateGui(MpController* controller)
{
	*(controller->getHostParameter(HC_CAN_UNDO)) = canUndo();
	*(controller->getHostParameter(HC_CAN_REDO)) = canRedo();
	*(controller->getHostParameter(HC_PROGRAM_MODIFIED)) = isPresetModified();
}

DawPreset const* UndoManager::push(std::string description, std::unique_ptr<const DawPreset> preset)
{
	if (undoPosition < size() - 1)
	{
		history.resize(undoPosition + 1);
	}
	auto raw = preset.get();

	undoPosition = size();
	history.push_back({ description, std::move(preset) });

#ifdef DEBUG_UNDO
	_RPT0(0, "UndoManager::push\n");
	debug();
#endif

	return raw;
}

void UndoManager::snapshot(MpController* controller, std::string description)
{
	if (!enabled)
		return;

	const auto couldUndo = canUndo();
	const auto couldRedo = canRedo();
	const auto wasModified = isPresetModified();

	push(description, controller->getPreset());

	if(!couldUndo || couldRedo || wasModified != isPresetModified()) // enable undo button
		UpdateGui(controller);

#ifdef DEBUG_UNDO
	_RPT0(0, "UndoManager::snapshot\n");
	debug();
#endif
}

void UndoManager::undo(MpController* controller)
{
	if (undoPosition <= 0 || undoPosition >= size())
		return;

	--undoPosition;

	auto& preset = history[undoPosition].second;
	preset->resetUndo = false;

	setPreset(controller, preset.get());

	// if we're back to the original preset, set modified=false.
	if (!canUndo())
		controller->setModified(false);

	UpdateGui(controller);

#ifdef DEBUG_UNDO
	_RPT0(0, "UndoManager::undo\n");
	debug();
#endif
}

void UndoManager::redo(MpController* controller)
{
	const auto next = undoPosition + 1;
	if (next < 0 || next >= size())
		return;

	auto& preset = history[next].second;

	preset->resetUndo = false;

	setPreset(controller, preset.get());

	undoPosition = next;

	controller->setModified(true);

	UpdateGui(controller);

#ifdef DEBUG_UNDO
	_RPT0(0, "UndoManager::redo\n");
	debug();
#endif
}

void UndoManager::getA(MpController* controller)
{
	if (AB_is_A)
		return;

	AB_is_A = true;

	auto current = push("Choose A", controller->getPreset());

	setPreset(controller, &AB_storage);

	AB_storage = *current;
}

void UndoManager::getB(MpController* controller)
{
	if (!AB_is_A)
		return;

	AB_is_A = false;

	// first time clicking 'B' just assign current preset to 'B'
	if (AB_storage.empty())
	{
		AB_storage = *controller->getPreset();
		return;
	}

	auto current = push("Choose B", controller->getPreset());

//	controller->setPreset(AB_storage);
	setPreset(controller, &AB_storage);
	AB_storage = *current;
}

void UndoManager::copyAB(MpController* controller)
{
	if (AB_is_A)
	{
		AB_storage = *controller->getPreset();
	}
	else
	{
		setPreset(controller, &AB_storage);
	}
}

void MpController::undoTransanctionStart()
{
	assert(undoManager.enabled);
	undoManager.enabled = false;
}

void MpController::undoTransanctionEnd()
{
	undoManager.enabled = true;
	undoManager.snapshot(this, "Change Parameters");
}

gmpi_gui::IMpGraphicsHost* MpController::getGraphicsHost()
{
#if !defined(SE_USE_JUCE_UI)
	for (auto g : m_guis2)
	{
		auto pa = dynamic_cast<GuiPatchAutomator3*>(g);
		if (pa)
		{
			auto gh = dynamic_cast<gmpi_gui::IMpGraphicsHost*>(pa->getHost());
			if (gh)
				return gh;
		}
	}
#endif

	return nullptr;
}

void MpController::OnSetHostControl(int hostControl, int32_t paramField, int32_t size, const void* data, int32_t voice)
{
	switch (hostControl)
	{
	case HC_PROGRAM:
		if (!inhibitProgramChangeParameter && paramField == gmpi::MP_FT_VALUE)
		{
			auto preset = RawToValue<int32_t>(data, size);

			MpParameter* programNameParam = nullptr;
			for (auto& p : parameters_)
			{
				if (p->getHostControl() == HC_PROGRAM_NAME)
				{
					programNameParam = p.get();
					break;
				}
			}

			if (preset >= 0 && preset < presets.size())
			{
				if (programNameParam)
				{
					const auto nameW = Utf8ToWstring(presets[preset].name);
					const auto raw2 = ToRaw4(nameW);
					const auto field = gmpi::MP_FT_VALUE;
					if(programNameParam->setParameterRaw(field, raw2.size(), raw2.data()))
					{
						programNameParam->updateProcessor(field, voice);
					}
				}

				if (presets[preset].isSession)
				{
					// set Preset(&session_preset);
					setPresetFromSelf(&session_preset);
				}
				else if (presets[preset].isFactory)
				{
					//loadFactoryPreset(preset, false);
					auto xml = getFactoryPresetXml(WStringToUtf8(presets[preset].filename));
					setPresetXmlFromSelf(xml);
				}
				else
				{
					std::string xml;
					if (presets[preset].filename.find(L".xmlpreset") != string::npos)
					{
						platform_string nativePath = toPlatformString(presets[preset].filename);
						FileToString(nativePath, xml);
					}
					else
					{
						xml = loadNativePreset(presets[preset].filename);
					}
					if (!xml.empty()) // cope with tester deleting preset file.
					{
						setPresetXmlFromSelf(xml);
					}
				}

// already done by setPresetXmlFromSelf				undoManager.initial(this, getPreset());

				setModified(false);
			}
		}
		break;


	case HC_PATCH_COMMANDS:
		if (paramField == gmpi::MP_FT_VALUE)
		{
			const auto patchCommand = *(int32_t*)data;

            if(patchCommand <= 0)
                break;
            
            // JUCE toolbar commands
			switch (patchCommand)
			{
			case (int) EPatchCommands::Undo:
				undoManager.undo(this);
				break;

			case (int)EPatchCommands::Redo:
				undoManager.redo(this);
				break;

			case (int)EPatchCommands::CompareGet_A:
				undoManager.getA(this);
				break;

			case (int)EPatchCommands::CompareGet_B:
				undoManager.getB(this);
				break;

			case (int)EPatchCommands::CompareGet_CopyAB:
				undoManager.copyAB(this);
				break;

			default:
				break;
			};

#if !defined(SE_USE_JUCE_UI)
            // L"Load Preset=2,Save Preset,Import Bank,Export Bank"
            if (patchCommand > 5)
                break;

			auto gh = getGraphicsHost();

			if (!gh)
                break;
            
            int dialogMode = (patchCommand == 2 || patchCommand == 4) ? 0 : 1; // load or save.
            nativeFileDialog = nullptr; // release any existing dialog.
            gh->createFileDialog(dialogMode, nativeFileDialog.GetAddressOf());

            if (nativeFileDialog.isNull())
                break;
            
            if (patchCommand > 3)
            {
                nativeFileDialog.AddExtension("xmlbank", "XML Bank");
                auto fullPath = WStringToUtf8(BundleInfo::instance()->getUserDocumentFolder());
                combinePathAndFile(fullPath.c_str(), "bank.xmlbank");
                nativeFileDialog.SetInitialFullPath(fullPath);
            }
            else // Load/Save Preset
            {
                const auto presetFolder = BundleInfo::instance()->getPresetFolder();
                CreateFolderRecursive(presetFolder);

				// default extension is the first one.
				if (getNativePresetExtension() == L"vstpreset")
				{
					nativeFileDialog.AddExtension("vstpreset", "VST3 Preset");
				}
				else
				{
					nativeFileDialog.AddExtension("aupreset", "Audio Unit Preset");
				}
                nativeFileDialog.AddExtension("xmlpreset", "XML Preset");

				// least-relevant option last
				if (getNativePresetExtension() == L"vstpreset")
				{
					nativeFileDialog.AddExtension("aupreset", "Audio Unit Preset");
				}
				else
				{
					nativeFileDialog.AddExtension("vstpreset", "VST3 Preset");
				}
                nativeFileDialog.AddExtension("*", "All Files");

				std::wstring initialPath = presetFolder;
				if (patchCommand == 3) // save
				{
					const auto parameterHandle = getParameterHandle(-1, -1 - HC_PROGRAM_NAME);
					if (auto it = ParameterHandleIndex.find(parameterHandle); it != ParameterHandleIndex.end())
					{
						auto p = (*it).second;
						const auto presetName = (std::wstring) p->getValueRaw(gmpi::FieldType::MP_FT_VALUE, 0);
						// Append preset name
						initialPath = combinePathAndFile(initialPath, presetName) + L'.' + getNativePresetExtension();
					}
				}
                nativeFileDialog.SetInitialFullPath(WStringToUtf8(initialPath));
            }

            nativeFileDialog.ShowAsync([this, patchCommand](int32_t result) -> void { this->OnFileDialogComplete(patchCommand, result); });
#endif
		}

		break;
	}
}

int32_t MpController::sendSdkMessageToAudio(int32_t handle, int32_t id, int32_t size, const void* messageData)
{
	auto queue = getQueueToDsp();

	// discard any too-big message.
	const auto totalMessageSize = 3 * static_cast<int>(sizeof(int)) + size;
	if (!my_msg_que_output_stream::hasSpaceForMessage(queue, totalMessageSize))
		return gmpi::MP_FAIL;

	my_msg_que_output_stream s(queue, (int32_t)handle, "sdk\0");
    
	s << (int32_t)(size + 2 * sizeof(int32_t)); // size of ID plus sizeof message.

	s << id;

	s << size;
	s.Write(messageData, size);

    s.Send();
    
	return gmpi::MP_OK;
}

void MpController::ParamToDsp(MpParameter* param, int32_t voice)
{
	assert(dynamic_cast<SeParameter_vst3_hostControl*>(param) == nullptr); // These have (not) "unique" handles that may map to totally random DSP parameters.

	//---send a binary message
	bool isVariableSize = param->datatype_ == DT_TEXT || param->datatype_ == DT_BLOB;

	auto raw = param->getValueRaw(gmpi::MP_FT_VALUE, voice);

	bool due_to_program_change = false;
	int32_t recievingMessageLength = (int)(sizeof(bool) + raw.size());
	if (isVariableSize)
	{
		recievingMessageLength += (int)sizeof(int32_t);
	}

	if (param->isPolyPhonic())
	{
		recievingMessageLength += (int)sizeof(int32_t);
	}

	constexpr int headerSize = sizeof(int32_t) * 2;
	const int totalMessageLength = recievingMessageLength + headerSize;

	if (totalMessageLength >= getQueueToDsp()->totalSpace())
	{
		_RPT0(0, "ERROR: MESSAGE TOO BIG FOR QUEUE\n");
		return;
	}

	{
		int timeout = 20;
		while (timeout-- > 0 && totalMessageLength > getQueueToDsp()->freeSpace())
		{
			this_thread::sleep_for(chrono::milliseconds(20));
		}

		if (timeout <= 0)
		{
			_RPT0(0, "ERROR: TIMOUT WAITING FOR QUEUE\n");
			return;
		}
	}

	my_msg_que_output_stream stream(getQueueToDsp(), param->parameterHandle_, "ppc\0"); // "ppc"

	stream << recievingMessageLength;
	stream << due_to_program_change;

	if (param->isPolyPhonic())
	{
		stream << voice;
	}

	if (isVariableSize)
	{
		stream << (int32_t)raw.size();
	}

	stream.Write(raw.data(), (unsigned int)raw.size());

	stream.Send();
}

void MpController::UpdateProgramCategoriesHc(MpParameter* param)
{
	ListBuilder_base<char> l;
	for (auto& preset : presets)
	{
		if(param->getHostControl() == HC_PROGRAM_CATEGORIES_LIST)
			l.Add(preset.category);
		else
		{
			assert(param->getHostControl() == HC_PROGRAM_NAMES_LIST);
			l.Add(preset.name);
		}
	}
	std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;

	auto enumList = convert.from_bytes(l.str());

	param->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(enumList));
}

MpParameter* MpController::createHostParameter(int32_t hostControl)
{
	SeParameter_vst3_hostControl* p = {};

	switch (hostControl)
	{
	case HC_PATCH_COMMANDS:
		p = new SeParameter_vst3_hostControl(this, hostControl);
		p->enumList_ = L"Load Preset=2,Save Preset,Import Bank,Export Bank";
		if (undoManager.enabled)
		{
			p->enumList_ += L", Undo=17, Redo";
		}
		break;

	case HC_CAN_UNDO:
	case HC_CAN_REDO:
		p = new SeParameter_vst3_hostControl(this, hostControl);
		break;

	case HC_PROGRAM:
	{
		p = new SeParameter_vst3_hostControl(this, hostControl);
		p->datatype_ = DT_INT;
		p->maximum = (std::max)(0.0, static_cast<double>(presets.size() - 1));
		const int32_t initialVal = -1; // ensure patch-browser shows <NULL> at first.
		RawView raw(initialVal);
		p->setParameterRaw(gmpi::MP_FT_VALUE, (int32_t)raw.size(), raw.data());
	}
	break;

	case HC_PROGRAM_NAME:
		p = new SeParameter_vst3_hostControl(this, hostControl);
		{
			auto raw2 = ToRaw4(L"Factory");
			p->setParameterRaw(gmpi::MP_FT_VALUE, (int32_t)raw2.size(), raw2.data());
		}
		break;

	case HC_PROGRAM_NAMES_LIST:
	{
		auto param = new SeParameter_vst3_hostControl(this, hostControl);
		p = param;
		p->datatype_ = DT_TEXT;

		UpdateProgramCategoriesHc(param);
	}
	break;

	case HC_PROGRAM_CATEGORIES_LIST:
	{
		auto param = new SeParameter_vst3_hostControl(this, hostControl);
		p = param;
		p->datatype_ = DT_TEXT;

		UpdateProgramCategoriesHc(param);
	}
	break;

	case HC_PROGRAM_MODIFIED:
	{
		auto param = new SeParameter_vst3_hostControl(this, hostControl);
		p = param;
		p->datatype_ = DT_BOOL;
	}
	break;

	case HC_PROCESSOR_OFFLINE:
	{
		auto param = new SeParameter_vst3_hostControl(this, hostControl);
		p = param;
		p->datatype_ = DT_BOOL;
	}
	break;


	/* what would it do?
	case HC_MIDI_CHANNEL:
	break;
	*/
	}

	if (!p)
		return {};

	p->stateful_ = false;

	// clashes with valid handles on DSP, ensure NEVER sent to DSP!!

	// generate unique parameter handle, assume all other parameters already registered.
	p->parameterHandle_ = 0;

	auto it = max_element(ParameterHandleIndex.begin(), ParameterHandleIndex.end(),
		[](const auto& i, const auto& j) {
			return i.first < j.first;
		});

	if(it != ParameterHandleIndex.end())
		p->parameterHandle_ = it->first + 1;

	ParameterHandleIndex.insert({ p->parameterHandle_, p });
	parameters_.push_back(std::unique_ptr<MpParameter>(p));

	return p;
}

MpParameter* MpController::getHostParameter(int32_t hostControl)
{
	const auto it = std::find_if(
		parameters_.begin()
		, parameters_.end()
		, [hostControl](std::unique_ptr<MpParameter>& p) {return p->getHostControl() == hostControl; }
	);
	
	if(it != parameters_.end())
		return (*it).get();

	return createHostParameter(hostControl);
}

int32_t MpController::getParameterHandle(int32_t moduleHandle, int32_t moduleParameterId)
{
	int hostControl = -1 - moduleParameterId;

	if (hostControl >= 0)
	{
		// why not just shove it in with negative handle? !!! A: because of potential attachment to container.
		for (auto& p : parameters_)
		{
			if (p->getHostControl() == hostControl && (moduleHandle == -1 || moduleHandle == p->ModuleHandle()))
			{
				return p->parameterHandle_;
				break;
			}
		}

		if (auto p = createHostParameter(hostControl); p)
		{
			return p->parameterHandle_;
		}
	}
	else
	{
		auto it = moduleParameterIndex.find(std::make_pair(moduleHandle, moduleParameterId));
		if (it != moduleParameterIndex.end())
			return (*it).second;
	}

	return -1;
}

void MpController::initializeGui(gmpi::IMpParameterObserver* gui, int32_t parameterHandle, gmpi::FieldType FieldId)
{
	auto it = ParameterHandleIndex.find(parameterHandle);

	if (it != ParameterHandleIndex.end())
	{
		auto p = (*it).second;

		for (int voice = 0; voice < p->getVoiceCount(); ++voice)
		{
			auto raw = p->getValueRaw(FieldId, voice);
			gui->setParameter(parameterHandle, FieldId, voice, raw.data(), (int32_t)raw.size());
		}
	}
}

bool MpController::onQueMessageReady(int recievingHandle, int recievingMessageId, class my_input_stream& p_stream)
{
	// Processor watchdog.
	if (dspWatchdogCounter <= 0)
	{
		for (auto& p : parameters_)
		{
			if (p->getHostControl() == HC_PROCESSOR_OFFLINE)
			{
				p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(false));
				break;
			}
		}
	}
	dspWatchdogCounter = dspWatchdogTimerInit;

	auto it = ParameterHandleIndex.find(recievingHandle);
	if (it != ParameterHandleIndex.end())
	{
		auto p = (*it).second;
		p->updateFromDsp(recievingMessageId, p_stream);
		return true;
	}
	else
	{
		switch(recievingMessageId)
		{
		case id_to_long2("sdk"):
		{
			struct DspMsgInfo2
			{
				int id;
				int size;
				void* data;
				int handle;
			};
			DspMsgInfo2 nfo;
			p_stream >> nfo.id;
			p_stream >> nfo.size;
			nfo.data = malloc(nfo.size);
			p_stream.Read(nfo.data, nfo.size);
			nfo.handle = recievingHandle;

			if (presenter_)
				presenter_->OnChildDspMessage(&nfo);

			free(nfo.data);

			return true;
		}
		break;

		case id_to_long2("ltnc"): // latency changed. VST3 or AU.
		{
			OnLatencyChanged();
		}
		break;

#if defined(_DEBUG) && defined(_WIN32) && 0 // BPM etc spam this
		default:
		{
			const char* msgstr = (const char*)&recievingMessageId;
			_RPT1(_CRT_WARN, "\nMpController::onQueMessageReady() Unhandled message id %c%c%c%c\n", msgstr[3], msgstr[2], msgstr[1], msgstr[0] );
		}
		break;
#endif
		}
	}
	return false;
}

bool MpController::OnTimer()
{
	message_que_dsp_to_ui.pollMessage(this);

	if (startupTimerCounter-- == 0)
	{
		OnStartupTimerExpired();
	}

	if (dspWatchdogCounter-- == 0)
	{
		for (auto& p : parameters_)
		{
			if (p->getHostControl() == HC_PROCESSOR_OFFLINE)
			{
				p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(true));
				break;
			}
		}
	}

	if (presetsFolderChanged)
	{
		presetsFolderChanged = false;
		ScanPresets();
		UpdatePresetBrowser();
	}

	return true;
}

void MpController::OnStartupTimerExpired()
{
	if (BundleInfo::instance()->getPluginInfo().emulateIgnorePC)
	{
		// class UniqueSnowflake
		enum { NONE = -1, DEALLOCATED = -2, APPLICATION = -4 };

		my_msg_que_output_stream s(getQueueToDsp(), /*UniqueSnowflake::*/ APPLICATION, "EIPC"); // Emulate Ignore Program Change
		s << (uint32_t)0;
		s.Send();
	}
}

bool MpController::ignoreProgramChangeActive() const
{
	return startupTimerCounter <= 0 && BundleInfo::instance()->getPluginInfo().emulateIgnorePC;
}

int32_t MpController::resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename)
{
	// copied from CSynthEditAppBase.

	std::wstring l_filename(shortFilename);
	std::wstring file_ext;
	file_ext = GetExtension(l_filename);

    const bool isUrl = l_filename.find(L"://") != string::npos;
    
    // Is this a relative or absolute filename?
#ifdef _WIN32
    const bool has_root_path = l_filename.find(L':') != string::npos;
#else
    const bool has_root_path = l_filename.size() > 0 && l_filename[0] == L'/';
#endif
    
	if (!has_root_path && !isUrl)
	{
//		auto default_path = BundleInfo::instance()->getImbedded FileFolder();
		const auto default_path = BundleInfo::instance()->getResourceFolder();

		l_filename = combine_path_and_file(default_path, l_filename);
	}

	if (l_filename.size() >= static_cast<size_t> (maxChars))
	{
		// return empty string (if room).
		if (maxChars > 0)
			returnFullFilename[0] = 0;

		return gmpi::MP_FAIL;
	}

	WStringToWchars(l_filename, returnFullFilename, maxChars);

	return gmpi::MP_OK;
}

void MpController::OnFileDialogComplete(int patchCommand, int32_t result)
{
	if (result == gmpi::MP_OK)
	{
		auto fullpath = nativeFileDialog.GetSelectedFilename();
		auto filetype = GetExtension(fullpath);
		bool isXmlPreset = filetype == "xmlpreset";

		switch (patchCommand) // L"Load Preset=2,Save Preset,Import Bank,Export Bank"
		{
		case 2:	// Load Preset
			if (isXmlPreset)
				ImportPresetXml(fullpath.c_str());
			else
			{
				auto xml = loadNativePreset( Utf8ToWstring(fullpath) );
				setPresetXmlFromSelf(xml);
			}
			break;

		case 3:	// Save Preset
			if (isXmlPreset)
				ExportPresetXml(fullpath.c_str());
			else
			{
				// Update preset name and category, so filename matches name in browser (else very confusing).
				std::wstring r_file, r_path, r_extension;
				decompose_filename(Utf8ToWstring(fullpath), r_file, r_path, r_extension);

				// Update program name and category (as they are queried by getPreset() ).
				for (auto& p : parameters_)
				{
					if (p->getHostControl() == HC_PROGRAM_NAME)
					{
						p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(r_file));
						p->updateProcessor(gmpi::FieldType::MP_FT_VALUE, 0); // Important that processor has correct name when DAW saves the session.
						updateGuis(p.get(), gmpi::FieldType::MP_FT_VALUE);
					}

					// Presets saved by user go into "User" category.
					if (p->getHostControl() == HC_PROGRAM_CATEGORY)
					{
						std::wstring category{L"User"};
						p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(category));
						p->updateProcessor(gmpi::FieldType::MP_FT_VALUE, 0);
						updateGuis(p.get(), gmpi::FieldType::MP_FT_VALUE);
					}
				}

				saveNativePreset(fullpath.c_str(), WStringToUtf8(r_file), getPreset()->toString(BundleInfo::instance()->getPluginId()));

				ScanPresets();
				UpdatePresetBrowser();

				// Update current preset name in browser.
				for (auto& p : parameters_)
				{
					if (p->getHostControl() == HC_PROGRAM)
					{
						auto nameU = WStringToUtf8(r_file);

						for (int32_t i = 0; i < presets.size(); ++i)
						{
							if (presets[i].name == nameU && presets[i].category == "User")
							{
								p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(i));

								updateGuis(p.get(), gmpi::FieldType::MP_FT_VALUE);
								break;
							}
						}
					}
				}

				setModified(false); // needed when overwritting the same preset name, becuase it don't trigger a change to HC_PROGRAM
			}
			break;

		case 4:
			ImportBankXml(fullpath.c_str());
			break;

		case 5:
			ExportBankXml(fullpath.c_str());
			break;
		}
	}

	nativeFileDialog.setNull(); // release it.
}

void MpController::ImportPresetXml(const char* filename, int presetIndex)
{
	platform_string nativePath = toPlatformString(filename);
	std::string newXml;
	FileToString(nativePath, newXml);

	setPresetXmlFromSelf(newXml);
}

std::unique_ptr<const DawPreset> MpController::getPreset(std::string presetNameOverride)
{
	auto preset = std::make_unique<DawPreset>();

	for (auto& p : parameters_)
	{
		if (p->getHostControl() == HC_PROGRAM_NAME)
		{
			preset->name = WStringToUtf8((std::wstring)p->getValueRaw(gmpi::FieldType::MP_FT_VALUE, 0));
			continue; // force non-save
		}
		if (p->getHostControl() == HC_PROGRAM_CATEGORY)
		{
			preset->category = WStringToUtf8((std::wstring)p->getValueRaw(gmpi::FieldType::MP_FT_VALUE, 0));
			continue; // force non-save
		}

		if (p->stateful_)
		{
			const auto paramHandle = p->parameterHandle_;
			auto& values = preset->params[paramHandle];

			values.dataType = (gmpi::PinDatatype)p->datatype_;

			const int voice = 0;
			const auto raw = p->getValueRaw(gmpi::MP_FT_VALUE, voice);
			values.rawValues_.push_back({ (char* const)raw.data(), raw.size() });

			// MIDI learn.
			if (p->MidiAutomation != -1)
			{
				values.MidiAutomation = p->MidiAutomation;			// "MIDI"
				values.MidiAutomationSysex = p->MidiAutomationSysex;// "MIDI_SYSEX"
			}
		}
	}

	if (!presetNameOverride.empty())
	{
		preset->name = presetNameOverride;
	}

	preset->name = SanitizeFileName(preset->name);

#if 0 // ??
	{
		char buffer[20];
		sprintf(buffer, "%08x", BundleInfo::instance()->getPluginId());
		element->SetAttribute("pluginId", buffer);
	}
#endif

	preset->calcHash();

	return preset; // dawStateManager.retainPreset(preset);
}

int32_t MpController::getParameterModuleAndParamId(int32_t parameterHandle, int32_t* returnModuleHandle, int32_t* returnModuleParameterId)
{
	auto it = ParameterHandleIndex.find(parameterHandle);
	if (it != ParameterHandleIndex.end())
	{
		auto seParameter = (*it).second;
		*returnModuleHandle = seParameter->moduleHandle_;
		*returnModuleParameterId = seParameter->moduleParamId_;
		return gmpi::MP_OK;
	}
	return gmpi::MP_FAIL;
}

RawView MpController::getParameterValue(int32_t parameterHandle, int32_t fieldId, int32_t voice)
{
	auto it = ParameterHandleIndex.find(parameterHandle);
	if (it != ParameterHandleIndex.end())
	{
		auto param = (*it).second;
		return param->getValueRaw((gmpi::FieldType) fieldId, 0);
	}

	return {};
}

void MpController::OnEndPresetChange()
{
	// try to 'debounce' multiple preset changes at startup.
	if (startupTimerCounter > 0)
	{
		startupTimerCounter = startupTimerInit;
	}
}

// new: set preset UI only. Processor is updated in parallel
void MpController::setPreset(DawPreset const* preset)
{
//	_RPTN(0, "MpController::setPreset. IPC %d\n", (int)preset->ignoreProgramChangeActive);
#if 0 //def _DEBUG
    auto xml = preset->toString(0);
    static int count = 0;
    count++;
    std::string filename("/Users/jeffmcclintock/log");
    filename += std::to_string(count) + ".txt";
    std::ofstream out(filename.c_str());
    out << xml;
#endif

	constexpr int patch = 0;
	constexpr bool updateProcessor = false;

	const std::wstring categoryNameW = Utf8ToWstring(preset->category);
	auto parameterHandle = getParameterHandle(-1, -1 - HC_PROGRAM_CATEGORY);
	auto it = ParameterHandleIndex.find(parameterHandle);
	if (it != ParameterHandleIndex.end())
	{
		auto p = (*it).second;
		p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(categoryNameW)); // don't check changed flag, if even originated from GUI, param is already changed. Still need top go to DSP.
/*
		if (updateProcessor)
		{
			p->updateProcessor(gmpi::MP_FT_VALUE, voiceId);
		}
*/
	}
	{
		const std::wstring nameW = Utf8ToWstring(preset->name);
		auto parameterHandle = getParameterHandle(-1, -1 - HC_PROGRAM_NAME);
		auto it = ParameterHandleIndex.find(parameterHandle);
		if (it != ParameterHandleIndex.end())
		{
			auto p = (*it).second;
			p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(nameW)); // don't check changed flag, if even originated from GUI, param is already changed. Still need top go to DSP.
/*
			if (updateProcessor)
			{
				p->updateProcessor(gmpi::MP_FT_VALUE, voiceId);
			}
*/
		}
	}

	for (const auto& [handle, val] : preset->params)
	{
		assert(handle != -1);

		auto it = ParameterHandleIndex.find(handle);
		if (it == ParameterHandleIndex.end())
			continue;

		auto& parameter = (*it).second;

		assert(parameter->datatype_ == (int)val.dataType);

		if (parameter->datatype_ != (int)val.dataType)
			continue;

		if (parameter->ignorePc_ && preset->ignoreProgramChangeActive && !preset->isInitPreset)
			continue;

		for (int voice = 0; voice < val.rawValues_.size(); ++voice)
		{
			const auto& raw = val.rawValues_[voice];

			// This block seems messy. Should updating a parameter be a single function call?
			// (would need to pass 'updateProcessor')
			{
				// calls controller_->updateGuis(this, voice)
				const auto changed = parameter->setParameterRaw(gmpi::MP_FT_VALUE, (int32_t)raw.size(), raw.data(), voice);

				// updated cached value.
				parameter->upDateImmediateValue();

				// Param will be updated in DSP independantly, but we still need to notify the DAW for non-private parameters.
				if (changed)
				{
					parameter->updateDaw();
				}

#if 0 //?
				if (updateProcessor) // For non-private parameters, update DAW.
				{
					parameter->updateProcessor(gmpi::MP_FT_VALUE, voice);
				}
#endif
			}
		}
	}

	if (preset->resetUndo)
	{
		auto copyofpreset = std::make_unique<DawPreset>(*preset);
		undoManager.initial(this, std::move(copyofpreset));
	}

	syncPresetControls(preset);
}

// after setting a preset, try to make sense of it in terms of the existing preset list.
void MpController::syncPresetControls(DawPreset const* preset)
{
	// if this is an undo/redo, no need to update preset list
	if (!preset->resetUndo)
		return;

	constexpr bool updateProcessor = false;

//	_RPTN(0, "syncPresetControls Preset: %s hash %4x\n", preset->name.c_str(), preset->hash);

	const std::string presetName = preset->name.empty() ? "Default" : preset->name;

	// When DAW loads preset XML, try to determine if it's a factory preset, and update browser to suit.
	int32_t presetIndex = -1; // exact match
	int32_t presetSameNameIndex = -1; // name matches, but not settings.

	/*
	XML will not match if any parameter was set outside the normalized range, because it will get clamped in the plugin.
	*/
  //  _RPT2(_CRT_WARN, "setPresetFromDaw: hash=%d\nXML:\n%s\n", (int) std::hash<std::string>{}(xml), xml.c_str());

	// If preset has no name, treat it as a (modified) "Default"
	//if (presetName.empty())
	//	presetName = "Default";

	// Check if preset coincides with a factory preset, if so update browser to suit.
	int idx = 0;
	for (const auto& factoryPreset : presets)
	{
		assert(factoryPreset.hash);

//		_RPTN(0, "                   factoryPreset: %s hash %4x\n", factoryPreset.name.c_str(), factoryPreset.hash);
		if (factoryPreset.hash == preset->hash)
		{
			presetIndex = idx;
			presetSameNameIndex = -1;
			//_RPT0(0, " same HASH!");
			break;
		}
		if (factoryPreset.name == presetName && !factoryPreset.isSession)
		{
			presetSameNameIndex = idx;
			//_RPT0(0, " same name");
		}

		//_RPT0(0, "\n");
		
		++idx;
	}

	if (presetIndex == -1)
	{
		if (presetSameNameIndex != -1)
		{
			// same name as an existing preset, but not the same parameter values.
			// assume it's the same preset, except it's been modified
			presetIndex = presetSameNameIndex;

			DawPreset const* preset{};

			// put original as undo state
			std::string newXml;
			if (presets[presetIndex].isFactory) // preset is contained in binary
			{
				newXml = getFactoryPresetXml(presets[presetIndex].name + ".xmlpreset");
			}
			else
			{
				if (presets[presetIndex].filename.find(L".xmlpreset") != string::npos)
				{
					platform_string nativePath = toPlatformString(presets[presetIndex].filename);
					FileToString(nativePath, newXml);
				}
				else
				{
					newXml = loadNativePreset(presets[presetIndex].filename);
				}
			}
			auto unmodifiedPreset = std::make_unique<DawPreset>(parametersInfo, newXml);
			undoManager.initial(this, std::move(unmodifiedPreset));

//			undoManager.snapshot(this, "Load Session Preset");
//			setModified(true);
		}
		else
		{
			// remove any existing "Session preset"
			presets.erase(
				std::remove_if(presets.begin(), presets.end(), [](presetInfo& preset) { return preset.isSession; })
				, presets.end()
			);
			
			// preset not available and not the same name as any existing ones, add it to presets as 'session' preset.
			presetIndex = static_cast<int32_t>(presets.size());

			presets.push_back(
			{
				presetName,
				preset->category,
				presetIndex,			// Internal Factory presets only.
				{},						// filename: External disk presets only.
				preset->hash,
				false,					// isFactory
				true					// isSession
				}
			);

			session_preset = *preset;
		}
	}

	{
		auto parameterHandle = getParameterHandle(-1, -1 - HC_PROGRAM);
		auto it = ParameterHandleIndex.find(parameterHandle);
		if (it != ParameterHandleIndex.end())
		{
			auto p = (*it).second;
			inhibitProgramChangeParameter = true;
//			_RPTN(0, "syncPresetControls Preset index: %d\n", presetIndex);
			if(p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(presetIndex)))
			{
				updateGuis(p, gmpi::FieldType::MP_FT_VALUE);
// VST2 only I think				p->updateProcessor(gmpi::FieldType::MP_FT_VALUE, 0); // Unusual. Informs VST2 DAW of program number.
			}

			inhibitProgramChangeParameter = false;
		}
		parameterHandle = getParameterHandle(-1, -1 - HC_PROGRAM_NAME);
		it = ParameterHandleIndex.find(parameterHandle);
		if (it != ParameterHandleIndex.end())
		{
			auto p = (*it).second;

			std::wstring name;
			if(presetIndex == -1)
			{
				const auto raw = p->getValueRaw(gmpi::FieldType::MP_FT_VALUE, 0);
				name = RawToValue<std::wstring>(raw.data(), raw.size());
			}
			else
			{
				// Preset found.
				name = Utf8ToWstring(presets[presetIndex].name);
			}

			if(p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(name)) && updateProcessor)
			{
				p->updateProcessor(gmpi::FieldType::MP_FT_VALUE, 0);
			}
		}
	}
}

bool MpController::isPresetModified()
{
	return undoManager.canUndo();
}

void MpController::SavePreset(int32_t presetIndex)
{
	const auto presetFolderW = BundleInfo::instance()->getPresetFolder();
	CreateFolderRecursive(presetFolderW);

	auto preset = getPresetInfo(presetIndex);
	const auto presetFolder = WStringToUtf8(presetFolderW);
	const auto fullPath = combinePathAndFile(presetFolder, preset.name) + ".xmlpreset";

	ExportPresetXml(fullPath.c_str());

	setModified(false);
	undoManager.initial(this, getPreset());

	ScanPresets();
	UpdatePresetBrowser();
}

void MpController::SavePresetAs(const std::string& presetName)
{
	const auto presetFolderW = BundleInfo::instance()->getPresetFolder();

	assert(!presetFolderW.empty()); // you need to call BundleInfo::initPresetFolder(manufacturer, product) when initializing this plugin.

	CreateFolderRecursive(presetFolderW);

	const auto presetFolder = WStringToUtf8(presetFolderW);
	const auto fullPath = combinePathAndFile(presetFolder, presetName ) + ".xmlpreset";

	ExportPresetXml(fullPath.c_str(), presetName);

	setModified(false);

	undoManager.initial(this, getPreset());

	ScanPresets();

	// Add new preset to combo
	UpdatePresetBrowser();

	// find the new preset and select it.
	for (int32_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex)
	{
		if (presets[presetIndex].name == presetName)
		{
			auto parameterHandle = getParameterHandle(-1, -1 - HC_PROGRAM);
			auto it = ParameterHandleIndex.find(parameterHandle);
			if (it != ParameterHandleIndex.end())
			{
				auto p = (*it).second;
				p->setParameterRaw(gmpi::FieldType::MP_FT_VALUE, RawView(presetIndex));
			}
			break;
		}
	}
}

void MpController::DeletePreset(int presetIndex)
{
	assert(presetIndex >= 0 && presetIndex < presets.size());

	auto parameterHandle = getParameterHandle(-1, -1 - HC_PROGRAM);
	auto it = ParameterHandleIndex.find(parameterHandle);
	if (it != ParameterHandleIndex.end())
	{
		auto p = (*it).second;

		auto currentPreset = (int32_t) p->getValueRaw(gmpi::FieldType::MP_FT_VALUE, 0);

		// if we're deleting the current preset, switch back to preset 0
		if (currentPreset == presetIndex)
		{
			int32_t newCurrentPreset = 0;
			(*p) = newCurrentPreset;
		}
	}
#if defined(__cpp_lib_filesystem)
	std::filesystem::remove(presets[presetIndex].filename);
#else
    std::remove(WStringToUtf8(presets[presetIndex].filename).c_str());
#endif

	ScanPresets();

	// update the relevant host-controls
	UpdatePresetBrowser();
}

// Note: Don't handle polyphonic stateful parameters.
void MpController::ExportPresetXml(const char* filename, std::string presetNameOverride)
{
	ofstream myfile;

#ifdef _WIN32
	auto unicode_filename = Utf8ToWstring(filename);
#else
	auto unicode_filename = filename;
#endif

	myfile.open(unicode_filename);

	myfile << getPreset(presetNameOverride)->toString(BundleInfo::instance()->getPluginId());

	myfile.close();
}

int32_t MpController::getCurrentPresetIndex()
{
	auto parameterHandle = getParameterHandle(-1, -1 - HC_PROGRAM);
	auto it = ParameterHandleIndex.find(parameterHandle);
	if (it != ParameterHandleIndex.end())
	{
		auto p = (*it).second;
		return (int32_t)p->getValueRaw(gmpi::FieldType::MP_FT_VALUE, 0);
	}

	return -1;
}

void MpController::ExportBankXml(const char* filename)
{
	// Create output XML document.
	tinyxml2::XMLDocument xml;
	xml.LinkEndChild(xml.NewDeclaration());

	auto presets_xml = xml.NewElement("Presets");
	xml.LinkEndChild(presets_xml);

	//Iterate  presets, combine them into bank, and export.

	const auto currentPreset = getCurrentPresetIndex();

	int presetIndex = 0;
	for (auto& preset : presets)
	{
		std::string chunk;

		// all presets can be retrieved from their file, except for the current preset. Which might be modified.
		if (presetIndex == currentPreset)
		{
			chunk = getPreset()->toString(BundleInfo::instance()->getPluginId());
		}
		else
		{
			chunk = loadNativePreset(ToWstring(preset.filename));
		}

		{
			tinyxml2::XMLDocument presetDoc;

			presetDoc.Parse(chunk.c_str());

			if (!presetDoc.Error())
			{
				auto parameters = presetDoc.FirstChildElement("Preset");
				auto copyOfParameters = parameters->DeepClone(&xml)->ToElement();
				presets_xml->LinkEndChild(copyOfParameters);
			}
		}

		++presetIndex;
	}

	// Save output XML document.
	xml.SaveFile(filename);
}

void MpController::ImportBankXml(const char* xmlfilename)
{
	const auto currentPreset = getCurrentPresetIndex();

	auto presetFolder = BundleInfo::instance()->getPresetFolder();

	CreateFolderRecursive(presetFolder);

//	TiXmlDocument doc; // Don't use tinyXML2. XML must match *exactly* the current format, including indent, declaration, everything. Else Preset Browser won't correctly match presets.
//	doc.LoadFile(xmlfilename);
	tinyxml2::XMLDocument doc;
	doc.LoadFile(xmlfilename);

	if (doc.Error())
	{
		assert(false);
		return;
	}

	auto presetsE = doc.FirstChildElement("Presets");

	for (auto PresetE = presetsE->FirstChildElement("Preset"); PresetE; PresetE = PresetE->NextSiblingElement())
	{
		// Query plugin's 4-char code. Presence Indicates also that preset format supports MIDI learn.
		int32_t fourCC = -1; // -1 = not specified.
		int formatVersion = 0;
		{
			const char* hexcode{};
			if (tinyxml2::XMLError::XML_SUCCESS == PresetE->QueryStringAttribute("pluginId", &hexcode))
			{
				formatVersion = 1;
				try
				{
					fourCC = std::stoul(hexcode, nullptr, 16);
				}
				catch (...)
				{
					// who gives a f*ck
				}
			}
		}

		// TODO !!! Check fourCC.

		const char* name{};
		if (tinyxml2::XML_SUCCESS != PresetE->QueryStringAttribute("name", &name))
		{
			PresetE->QueryStringAttribute("Name", &name); // old format used to be capitalized.
		}
		auto filename = presetFolder + Utf8ToWstring(name) + L".";
		filename += getNativePresetExtension();

		// Create a new XML document, containing only one preset.
		tinyxml2::XMLDocument doc2;
		doc2.LinkEndChild(doc2.NewDeclaration());// new TiXmlDeclaration("1.0", "", "") );
		doc2.LinkEndChild(PresetE->DeepClone(&doc2));

		tinyxml2::XMLPrinter printer;
//		printer. .SetIndent(" ");
		doc2.Accept(&printer);
		const std::string presetXml{ printer.CStr() };

		// dialog if file exists.
//		auto result = gmpi::MP_OK;

/* no mac support
		fs::path fn(filename);
		if (fs::exists(fn))
*/
#ifdef _WIN32
        auto file = _wfopen(filename.c_str(), L"r"); // fs::exists(filename)
#else
        auto file = fopen(WStringToUtf8(filename).c_str(), "r");
#endif
        if(file)
		{
			fclose(file);

			auto gh = getGraphicsHost();

			if (gh)
			{
				okCancelDialog.setNull(); // free previous.
				gh->createOkCancelDialog(0, okCancelDialog.GetAddressOf());

				if (okCancelDialog.isNull())
					return;

				std::ostringstream oss;
				oss << "Overwrite preset '" << name << "'?";
				okCancelDialog.SetText(oss.str().c_str());

				okCancelDialog.ShowAsync([this, name, presetXml, filename] (int32_t result) -> void
					{
						if( result == gmpi::MP_OK )
                            saveNativePreset(
                                 WStringToUtf8(filename).c_str(),
                                 name,
                                 presetXml
                            );
					}
				);
			}
		}
		else
		{
			saveNativePreset(WStringToUtf8(filename).c_str(), name, presetXml);
		}
	}

	ScanPresets();
	OnSetHostControl(HC_PROGRAM, gmpi::MP_FT_VALUE, sizeof(currentPreset), &currentPreset, 0);
	UpdatePresetBrowser();
}

void MpController::setModified(bool presetIsModified)
{
	(*getHostParameter(HC_PROGRAM_MODIFIED)) = presetIsModified;
}

void MpController::ResetProcessor2()
{
	my_msg_que_output_stream s(getQueueToDsp(), UniqueSnowflake::APPLICATION, "RSRT");

	s << (int)0;
	s.Send();
}
