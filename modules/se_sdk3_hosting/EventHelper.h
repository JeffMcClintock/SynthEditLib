//
//  EventHelper.h
//  Standalone
//
//  Created by Jenkins on 22/09/17.
//  Copyright © 2017 Jenkins. All rights reserved.
//

#ifndef EventHelper_h
#define EventHelper_h

#include "modules/se_sdk3_hosting/CocoaNamespaceMacros.h"

//#define SYNTHEDIT_EVENT_HELPER_CLASSNAME SE_PASTE_MACRO4(EventHelper,SE_MAJOR_VERSION ,SE_MINOR_VERSION , SE_BUILD_NUMBER)
#define SYNTHEDIT_EVENT_HELPER_CLASSNAME SE_MAKE_CLASSNAME(CocoaEventHelper)


// Renamed from EventHelperClient to avoid global-namespace clash with gmpi_ui's
// EventHelperClient (in backends/MacTextEdit.h). They differ in member function set,
// so cannot coexist in one translation unit.
struct CocoaEventHelperClient
{
    virtual void CallbackFromCocoa(NSObject* sender) = 0;
};


@interface SYNTHEDIT_EVENT_HELPER_CLASSNAME : NSObject {
    CocoaEventHelperClient* client;

}

- (void)initWithClient:(CocoaEventHelperClient*)client;
- (void)menuItemSelected: (id) sender;
- (void)endEditing: (id) sender;

@end

#endif /* EventHelper_h */
