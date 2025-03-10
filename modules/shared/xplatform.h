#pragma once

/*
#include "../shared/xplatform.h"
*/

#include "./platform_string.h"

/*
* todo: BETTER TO SUPPORT FEATURES, e.g. 'SEM support' NOT 'JUCE' or 'VST3'
* 
  Platform				Defined
  SEM Plugin			SE_TARGET_SEM (selected SEMs)
  AU Plugin                         					  	SE_EXTERNAL_SEM_SUPPORT=1
  VST2/3 Plugin(64-bit)									  	SE_EXTERNAL_SEM_SUPPORT=1	SE_TARGET_VST3 (mac only)
  JUCE Plugin			GMPI_IS_PLATFORM_JUCE==1		  	SE_EXTERNAL_SEM_SUPPORT=0
  SynthEdit.exe			SE_EDIT_SUPPORT						SE_EXTERNAL_SEM_SUPPORT=1
  SynthEdit.exe (store)	SE_EDIT_SUPPORT						SE_EXTERNAL_SEM_SUPPORT=1	SE_TARGET_STORE
  
  SE_TARGET_PLUGIN  - Code is running in a plugin (VST, AU, JUCE etc)

  no longer in use: SE_TARGET_AU, SE_SUPPORT_MFC
*/

#if !defined(GMPI_IS_PLATFORM_JUCE) // allow this to be set in cmake for SE2JUCE projects
#if defined(JUCE_APP_VERSION)
#define GMPI_IS_PLATFORM_JUCE 1
#else
#define GMPI_IS_PLATFORM_JUCE 0
#endif
#endif

// External plugins not supported on JUCE
#if !GMPI_IS_PLATFORM_JUCE
	#define SE_EXTERNAL_SEM_SUPPORT 1
#else
	#define SE_EXTERNAL_SEM_SUPPORT 0
#endif

#ifdef _WIN32
	#define PLATFORM_PATH_SLASH '\\'
	#define PLATFORM_PATH_SLASH_L L'\\'
#else
	#define PLATFORM_PATH_SLASH '/'
	#define PLATFORM_PATH_SLASH_L L'/'
    #define MAX_PATH 500
#endif
