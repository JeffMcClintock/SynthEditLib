#pragma once

#ifndef SynthEditCocoaView_h
#define SynthEditCocoaView_h

#import <Cocoa/Cocoa.h>

#if !GMPI_IS_PLATFORM_JUCE && !defined(SE_TARGET_VST3)
#import <AudioUnit/AUCocoaUIView.h>
#endif

#include "CocoaNamespaceMacros.h"

#endif /* SynthEditCocoaView_h */
