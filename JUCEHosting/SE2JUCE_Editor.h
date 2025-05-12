#pragma once
#include <JuceHeader.h>

#ifdef _WIN32
#include "Shared/DrawingFrame2_win.h"

class JuceDrawingFrame : public DrawingFrameHwndBase, public juce::HWNDComponent
{
public:
    void setWindowHandle(HWND hwnd) override { setHWND((void*)hwnd); }
    HWND getWindowHandle() override { return (HWND) getHWND(); }
};
#else

class JuceDrawingFrame : public juce::NSViewComponent
{
public:
    ~JuceDrawingFrame();
    void open(class IGuiHost2* controller, int width, int height);
};

#endif

//==============================================================================

class SynthEditEditor :
    public juce::AudioProcessorEditor
{
public:
    SynthEditEditor (class SE2JUCE_Processor&, class SeJuceController&);
    
    //==============================================================================
    void parentHierarchyChanged() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SeJuceController& controller;
    JuceDrawingFrame drawingframe;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthEditEditor)
};
