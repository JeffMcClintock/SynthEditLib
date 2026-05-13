#ifndef CocoaGuiHost_h
#define CocoaGuiHost_h

// Sole survivor of what used to be a hosting layer for native Cocoa dialogs.
// Platform dialog classes (PlatformMenu, PlatformTextEntry, PlatformFileDialog,
// PlatformOkCancelDialog) have all been migrated to gmpi_ui's single-header
// backends. This helper is kept because SynthEditCocoaView.mm still calls it
// when invalidating the backing-buffer rect.

#import "./Cocoa_Gfx.h"

namespace GmpiGuiHosting
{
    inline NSRect gmpiRectToViewRect(NSRect viewbounds, GmpiDrawing::Rect rect)
    {
        #if USE_BACKING_BUFFER
            // flip co-ords
            return NSMakeRect(
              rect.left,
              viewbounds.origin.y + viewbounds.size.height - rect.bottom,
              rect.right - rect.left,
              rect.bottom - rect.top
              );
        #else
            return NSMakeRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
        #endif
    }
} // namespace

#endif /* CocoaGuiHost_h */
