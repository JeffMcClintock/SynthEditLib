#pragma once
#include <string>
#include <algorithm>
#include "../tinyXml2/tinyxml2.h"
#ifdef _WIN32
#include "shlobj.h"
#else
#include <CoreFoundation/CoreFoundation.h>
#include <pwd.h>
#endif
#include "../shared/unicode_conversion2.h"
#include <sys/stat.h>

namespace SettingsFile
{
	using namespace tinyxml2;

	// Determine settings file: C:\Users\Jeff\AppData\Local\Plugin\Preferences.xml
	inline std::string getSettingFilePath(std::wstring product)
	{
#ifdef _WIN32
		wchar_t mySettingsPath[MAX_PATH];
		SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, mySettingsPath);
		std::wstring meSettingsFile(mySettingsPath);
		meSettingsFile += L"/";
#else
        // macOS: locate ~/Library/Preferences/.
        //
        // CAUTION (sandboxing): inside an App Sandbox the system redirects HOME to the
        // app's container, e.g. /Users/<user>/Library/Containers/<bundle-id>/Data, so
        // getenv("HOME") returns that container path, NOT /Users/<user>. Containers are
        // keyed per bundle-id, so each sandboxed host (Logic, GarageBand, all AUv3
        // extensions) gets its own isolated copy of this settings file. For licensing
        // this means a registration made in one host is invisible to another -> the user
        // is asked to register again. Unsandboxed DAWs (Reaper, Live, Cubase, Bitwig...)
        // are unaffected: there HOME is the real home and this code behaves as intended.
        //
        // Note: getpwuid(getuid())->pw_dir below returns the REAL home (/Users/<user>)
        // even inside a sandbox, but it is only used as a fallback when HOME is unset --
        // and in a sandbox HOME is set (to the container), so this branch does not run.
        // Switching to getpwuid unconditionally is NOT a fix for the licensing problem:
        // the sandbox restricts file access by PATH, not by the value of HOME, so writing
        // to the real ~/Library/Preferences from a sandboxed host fails with EPERM. A
        // machine-wide license shared across hosts needs an installer-written shared path
        // (e.g. /Library/Application Support/<vendor>/) or the Keychain instead.
        const char* homeDir = getenv("HOME");

        if(!homeDir)
        {
            struct passwd* pwd = getpwuid(getuid());
            if (pwd)
                homeDir = pwd->pw_dir;
        }

        auto meSettingsFile = FastUnicode::Utf8ToWstring(homeDir) + L"/Library/Preferences/";
#endif
		meSettingsFile += product;

		// Create folder if not already.
#ifdef _WIN32
		_wmkdir(meSettingsFile.c_str());
#else
		mkdir(FastUnicode::WStringToUtf8(meSettingsFile).c_str(), 0775);
#endif

		meSettingsFile += L"/Preferences.xml";

		return  FastUnicode::WStringToUtf8(meSettingsFile);
	}

	inline std::wstring Sanitize(std::wstring s)
	{
		wchar_t remove[] = { L'!', L'#', L'$', L'%', L'&', L'(', L')', L'*', L'+', L',', L'.', L'/', L':', L';', L'<', L'=', L'>', L'?', L'@', L'[', L'\\', L']', L'^', L'{', L'|', L'}', L'~' };

		for (auto c : remove)
		{
			s.erase(std::remove(s.begin(), s.end(), c), s.end());
		}

		// replace with underscore.
		wchar_t replace[] = { L' ', L'-' };

		for (auto c : replace)
		{
			std::replace(s.begin(), s.end(), c, L'_');
		}

		return s;
	}

	inline std::wstring GetValue(std::wstring product, std::wstring key, std::wstring pdefault)
	{
		key = Sanitize(key);

		tinyxml2::XMLDocument doc;

        if (XML_SUCCESS == doc.LoadFile(getSettingFilePath(product).c_str()) && !doc.Error())
		{
			auto settingsXml = doc.FirstChildElement("Preferences");

			if (settingsXml)
			{
				auto keyXml = settingsXml->FirstChildElement(FastUnicode::WStringToUtf8(key).c_str());

				if (keyXml)
				{
					auto s = keyXml->GetText();
					return s ? FastUnicode::Utf8ToWstring(s) : L"";
				}
			}
		}

		return pdefault;
	}

	inline void SetValue(std::wstring product, std::wstring pkey, std::wstring value)
	{
		pkey = Sanitize(pkey);

		// Load Settings XML document
		const auto settingFilename = getSettingFilePath(product);
		tinyxml2::XMLDocument doc;

		// If document don't exist, create it.
		XMLElement* element_preferences = nullptr;
		if (XML_SUCCESS != doc.LoadFile(settingFilename.c_str()) || doc.Error())
		{
			doc.Clear();
			doc.ClearError();

			doc.LinkEndChild(doc.NewDeclaration());
		}
		else
		{
			element_preferences = doc.FirstChildElement("Preferences");
		}

		// If preferences element don't exist, create it.
		if (!element_preferences)
		{
			element_preferences = doc.NewElement("Preferences");
			doc.LinkEndChild(element_preferences);
		}

		auto key = FastUnicode::WStringToUtf8(pkey);

		auto keyXml = element_preferences->FirstChildElement(key.c_str());

		// If key element don't exist, create it.
		if (!keyXml)
		{
			keyXml = doc.NewElement(key.c_str());
			element_preferences->LinkEndChild(keyXml);
		}

		// If key value element don't exist, create it.
		auto textValue = FastUnicode::WStringToUtf8(value);
		XMLText* textE = nullptr;

		if (keyXml->FirstChild())
			textE = keyXml->FirstChild()->ToText();

		if (!textE)
		{
			textE = doc.NewText(textValue.c_str());
			keyXml->LinkEndChild(textE);
		}
		else
		{
			textE->SetValue(textValue.c_str());
		}

		doc.SaveFile(settingFilename.c_str());
	}
}
