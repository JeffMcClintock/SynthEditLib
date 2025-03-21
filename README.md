<img src="Docs/Images/SE2JUCE.gif"/>

# Introduction 
SE2JUCE supports exporting SynthEdit projects to C++ JUCE projects.

JUCE Projects
* Can target more plugin formats than SynthEdit alone, like AAX, Standalone, VST2 and CLAP etc.
* Hide all resources and SEMs inside a single binary file.
* Can display the GUI you created in SynthEdit, or a custom GUI made with JUCE.

(SynthEdit GUIs are supported on macOS and Windows, but not Linux or IOS)

Note that using SynthEditlib with JUCE is a complex, advanced proceedure that involves programming in C++ and having an understanding of CMake and JUCE.
You need to have access to the source-code of any SEMs you wish to use. This may not be possible in some cases,
 especially with 3rd-party modules. 3rd-party module creators have no obligation to share their source-code.

# Prerequisites

Install SynthEdit 1.6 https://synthedit.com/downloads/?url=/downloads

Install Visual Studio or your IDE of choice. https://visualstudio.microsoft.com/vs/

Install CMake. https://cmake.org/download/

# Getting Started
Move your synthedit project file into a folder structure like /MyProject/SE_Project/MyProject.se1

In SynthEdit open project from /MyProject/SE_Project/MyProject.se1

Choose menu "File/Export Juce" This will copy the project and its skin to the '/MyProject/Resources' folder of the JUCE project.

close SE

Open CMake GUI

Under "where is the source code" enter the location of /MyProject folder

Under "where to build the binaries" enter something like .../MyProject/build (or anywhere you prefer to put the temporary files created during the build).

Click 'Configure", and choose whatever IDE you prefer. Ignore the error message.

tick 'JUCE_COPY_PLUGIN_AFTER_BUILD'

click 'generate'

click 'open project' (your IDE should open)

build and try out the plugin

get JUCE. https://juce.com/get-juce/download

Optional:
* add VST2 headers to JUCE if you need to make VST2 plugins.
* get AAX SDK if you need it. https://www.avid.com/alliance-partner-program/aax-connectivity-toolkit

# Azure pipelines (advanced)
SynthEdit will create some scripts in the pipelines folder for building your plugin on Azure devops.
You will need an Azure devops account to do this.
You will need your code stored online in a git repo.

On the Azure website, go to 'pipelines' and create a new pipeline. Choose your git repo and select the 'Existing Azure Pipelines YAML file' option.
Select 'MyPlugin/pipelines/P_00.yml' as the script file.
From the 'Run' dropdown select "Save".
Next to "Run Pipeline", click the 3 vertical dots at the right 'Rename' it to e.g. "00 MyPlugin Start Build"

Repeat these steps for each of the 5 pipeline scripts.

# Notes about pipelines
When you run the pipelines they will complain about "TODO inset UUID of overall project here".
You need to find the UUID and number of the previous pipeline and insert that. The easiest way is to use the task wizard at right
 to insert a 'Download Artifacts' task and copy the values off that.

The macOS build will expect a user guide to be in teh main folder /MyPlugin/MyPluginUserGuide.pdf


# Missing modules

Once you start exporting your own plugins with SE2JUCE you will likely experience crashes due to the plugin not containing some module it requires.

To identify which modules need to be added to the project, run the plugin or standalone app in a debugger. ('Set as Startup Project' the Standalone, set Solution Configuration to 'Debug', press 'Start Debugging F5')
<img src="Docs/Images/SE2JUCE_MIssingModule3.PNG"/>

When you run the standalone, it will crash (assert) at the point where it is trying to load the missing modules. (hit 'Retry' to break into the debugger)

<img src="Docs/Images/SE2JUCE_MIssingModule1.PNG"/>

You should see in the 'Output' Window a list of the missing modules.
<img src="Docs/Images/SE2JUCE_MIssingModule2.PNG"/>

In the example above, its the 'SE Keyboard (MIDI)' that is missing from the build.

# Adding an additional module

To add an extra module to the build you will need access to its source code. In the case of the keyboard, the code is in the *SE2JUCE\SE_DSP_CORE\modules\ControlsXp* folder, but not actually included in the build yet.

Open the *SE2JUCE_Plugins\PD303\CMakeLists.txt* file, look for the part that mentions 'Adsr4.cpp'. Add an additional reference to the new module you want to include. If the module has both GUI and DSP parts, add both.
<img src="Docs/Images/SE2JUCE_AddModule1.png"/>

Now open the file *SE2JUCE_Plugins/PD303/Source/ExtraModules.cpp* and add lines like the following.
<img src="Docs/Images/SE2JUCE_AddModule2.png"/>

Finally, add the *SE_DECLARE_INIT_STATIC_FILE* macro line to each module file (if not already done)
<img src="Docs/Images/SE2JUCE_AddModule3.png"/>

Build and run the Standalone app. The Keyboard module now works in the JUCE plugin.
<img src="Docs/Images/SE2JUCE_AddModule4.png"/>

# Presets
The plugin you build with SE2JUCE will save presets in xmlformat. Any preset the user saves will go to the folder:

Windows: C:\ProgramData\\*YourVendorName*\\*YourPluginName*\USER Presets

Mac: /Library/Application Support/*YourVendorName*/*YourPluginName*/USER Presets

Your plugin can also include read-only factory presets. To include any factory preset into the plugin binary, add it to the folder Resources/presets


# Further help and information

SynthEdit - https://groups.io/g/synthedit

JUCE - https://forum.juce.com/

AAX - https://www.avid.com/alliance-partner-program/aax-connectivity-toolkit

CLAP - https://github.com/free-audio/clap-juce-extensions

VST2 - https://forum.juce.com/t/how-to-offer-vst2-plugins-now/39195

