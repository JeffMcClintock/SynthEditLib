#pragma once
#include "experimental/theme.h"
#include <functional>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace SynthEdit
{

// Enum for theme preference
enum class ThemePreference
{
	SystemDefault,  // Follow system theme
	Light,          // Force light theme
	Dark            // Force dark theme
};

#ifdef _WIN32

class ThemeManager
{
public:
	using ThemeChangeCallback = std::function<void(gmpi::ui::ThemeMode)>;

	static ThemeManager& instance()
	{
		static ThemeManager mgr;
		return mgr;
	}

	// Initialize and detect current system theme
	void initialize();

	// Early initialization from settings file path (call before UI is created)
	void initializeFromSettingsFile(const std::string& settingsFilePath);

	// Set theme preference
	void setPreference(ThemePreference pref);

	// Get current preference
	ThemePreference getPreference() const { return preference_; }

	// Get effective theme mode (resolved from preference)
	gmpi::ui::ThemeMode getEffectiveTheme() const;

	// Register callback for theme changes
	void addThemeChangeListener(ThemeChangeCallback callback);

	// Handle Windows WM_SETTINGCHANGE message
	void onSystemThemeChanged();

private:
	ThemeManager() = default;
	~ThemeManager() = default;
	ThemeManager(const ThemeManager&) = delete;
	ThemeManager& operator=(const ThemeManager&) = delete;

	// Detect Windows system theme from registry
	bool isWindowsInDarkMode() const;

	// Apply theme and notify listeners
	void applyTheme(gmpi::ui::ThemeMode mode);

	ThemePreference preference_ = ThemePreference::SystemDefault;
	gmpi::ui::ThemeMode currentMode_ = gmpi::ui::ThemeMode::Dark;
	std::vector<ThemeChangeCallback> listeners_;
};

#endif // _WIN32

// Preference string conversion helpers
inline std::string themePreferenceToString(ThemePreference pref)
{
	switch (pref)
	{
	case ThemePreference::Light: return "Light";
	case ThemePreference::Dark: return "Dark";
	default: return "SystemDefault";
	}
}

inline ThemePreference themePreferenceFromString(const std::string& str)
{
	if (str == "Light") return ThemePreference::Light;
	if (str == "Dark") return ThemePreference::Dark;
	return ThemePreference::SystemDefault;
}

} // namespace SynthEdit
