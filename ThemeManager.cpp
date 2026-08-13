#include "ThemeManager.h"

#ifdef _WIN32

#include <winreg.h>
#include "../tinyxml2/tinyxml2.h"

namespace SynthEdit
{

void ThemeManager::initializeFromSettingsFile(const std::string& settingsFilePath)
{
	// Read just the theme preference from settings before any UI is created.
	// Settings are stored as XML attributes (not child elements), e.g.:
	//   <ApplicationSettings ThemePreference="Dark" ... />
	tinyxml2::XMLDocument xmldoc;
	if (tinyxml2::XML_SUCCESS == xmldoc.LoadFile(settingsFilePath.c_str()))
	{
		if (auto root = xmldoc.FirstChildElement("ApplicationSettings"))
		{
			const char* value = nullptr;
			if (tinyxml2::XML_SUCCESS == root->QueryStringAttribute("ThemePreference", &value))
			{
				preference_ = themePreferenceFromString(value);
			}
		}
	}

	initialize();
}

void ThemeManager::initialize()
{
	// Detect initial system theme
	const bool isDark = isWindowsInDarkMode();
	currentMode_ = isDark ? gmpi::ui::ThemeMode::Dark : gmpi::ui::ThemeMode::Light;

	// Always sync global gmpi::ui theme mode (it defaults to Dark)
	const auto effective = getEffectiveTheme();
	currentMode_ = effective;
	gmpi::ui::setThemeMode(effective);
}

void ThemeManager::setPreference(ThemePreference pref)
{
	if (preference_ == pref)
		return;

	preference_ = pref;
	applyTheme(getEffectiveTheme());
}

gmpi::ui::ThemeMode ThemeManager::getEffectiveTheme() const
{
	switch (preference_)
	{
	case ThemePreference::Light:
		return gmpi::ui::ThemeMode::Light;
	case ThemePreference::Dark:
		return gmpi::ui::ThemeMode::Dark;
	case ThemePreference::SystemDefault:
	default:
		return isWindowsInDarkMode() ? gmpi::ui::ThemeMode::Dark : gmpi::ui::ThemeMode::Light;
	}
}

void ThemeManager::addThemeChangeListener(ThemeChangeCallback callback)
{
	listeners_.push_back(callback);
}

void ThemeManager::onSystemThemeChanged()
{
	// Only react if we're following system theme
	if (preference_ == ThemePreference::SystemDefault)
	{
		const auto newMode = getEffectiveTheme();
		if (newMode != currentMode_)
		{
			applyTheme(newMode);
		}
	}
}

bool ThemeManager::isWindowsInDarkMode() const
{
	// Check Windows 10/11 personalization settings
	// HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
	// "AppsUseLightTheme" (0 = dark, 1 = light)

	HKEY hKey;
	DWORD value = 1; // default to light
	DWORD dataSize = sizeof(value);

	const LPCWSTR keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
	const LPCWSTR valueName = L"AppsUseLightTheme";

	if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		RegQueryValueExW(hKey, valueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &dataSize);
		RegCloseKey(hKey);
	}

	// 0 = dark mode, 1 (or any other value) = light mode
	return value == 0;
}

void ThemeManager::applyTheme(gmpi::ui::ThemeMode mode)
{
	if (currentMode_ == mode)
		return;

	currentMode_ = mode;

	// Update gmpi::ui global theme
	gmpi::ui::setThemeMode(mode);

	// Notify all listeners
	for (auto& callback : listeners_)
	{
		callback(mode);
	}
}

} // namespace SynthEdit

#endif // _WIN32
