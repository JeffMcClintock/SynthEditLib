#import <Foundation/Foundation.h>

#import "SynthEditCocoaView.h"
#include "CocoaNamespaceMacros.h"

#include "./CocoaGuiHost.h"
#import "./ContainerView.h"
#include "./JsonDocPresenter.h"
#include "BundleInfo.h"
#include "backends/CocoaGfx.h"
#include "backends/DrawingFrameMac.h"
#include "legacy_sdk_gui2.h"

#if defined(SE_TARGET_AU)
#include "../../../se_au/SEInstrumentBase.h"
#endif

// In VST3 wrapper this object is a child window of SynthEditPluginCocoaView,
// It serves to provide a C++ to Objective-C adaptor to the gmpi Drawing framework.
// (actual parent is objective c).
namespace Json
{
    class Value;
}

class DrawingFrameCocoa : public
      gmpi::api::IDrawingHost
    , gmpi::api::IDialogHost
    , gmpi_gui::legacy::IMpGraphicsHost
    , gmpi::legacy::IMpUserInterfaceHost2
    , GmpiGuiHosting::PlatformTextEntryObserver
{
    gmpi_sdk::mp_shared_ptr<SE2::ViewBase> containerView;
    IGuiHost2* controller;
    int32_t mouseCaptured = 0;
    GmpiGuiHosting::PlatformTextEntry* currentTextEdit = nullptr;
    GmpiGuiHosting::UpdateRegionMac dirtyRects;

public:
    se::cocoa::DrawingFactory drawingFactory_SDK3;
    gmpi::cocoa::Factory drawingFactory_GMPI;

    NSView* view;
    NSBitmapImageRep* backBuffer{}; // backing buffer with linear colorspace for correct blending.

    DrawingFrameCocoa() : drawingFactory_SDK3(drawingFactory_GMPI.info){}
    
    void Init(SE2::IPresenter* presenter, class IGuiHost2* hostPatchManager, int pviewType)
    {
        controller = hostPatchManager;
        initFactoryHelper(drawingFactory_GMPI.info);
        
        const int topViewBounds = 8000; // simply a large enough size.
        containerView.Attach(new SE2::ContainerViewPanel({topViewBounds,topViewBounds}));
        containerView->setHost(static_cast<gmpi_gui::legacy::IMpGraphicsHost*>(this));
        
        containerView->setDocument(presenter);
        
#if defined(SE_TARGET_AU)
        dynamic_cast<SEInstrumentBase*>(controller)->callbackOnUnloadPlugin = [this]
        {
            containerView = nullptr; // free all objects early to avoid dangling pointers to AudioUnit.
            controller = nullptr; // ensure we don't reference in in destructor.
        };
#endif
     }
     
     void DeInit()
     {
        containerView = {};
     }

    ~DrawingFrameCocoa()
    {
#if defined(SE_TARGET_AU)
        auto audioUnit = dynamic_cast<SEInstrumentBase*>(controller);
        if(audioUnit)
        {
            audioUnit->callbackOnUnloadPlugin = nullptr;
        }
#endif
// alternative?
// controller->OnDrawingFrameDeleted(); // nulls it's 'OnUnloadPlugin' callback
    }

    inline SE2::ViewBase* getView()
    {
        return containerView.get();
    }
    
    void OnRender(NSView* frame /*, GmpiDrawing_API::MP1_RECT* dirtyRect*/)
    {
#if USE_BACKING_BUFFER
        if(!backBuffer)
            initBackingBitmap();
        
        // draw onto linear back buffer.
        [NSGraphicsContext saveGraphicsState];
 // ??       [backBuffer retain];
        NSGraphicsContext *g = [NSGraphicsContext graphicsContextWithBitmapImageRep:backBuffer];

        [NSGraphicsContext setCurrentContext:g];

        auto flipper = [NSAffineTransform transform];
        [flipper scaleXBy:1 yBy:-1];
        [flipper translateXBy:0.0 yBy:-[frame bounds].size.height];
        [flipper concat];

        dirtyRects.optimizeRects();

        if(-1 == se::cocoa::GraphicsContext2::logicProFix)
        {
            se::cocoa::GraphicsContext2::logicProFix = 0;
            
            se::cocoa::UniversalGraphicsContext context(frame, &drawingFactory_GMPI, &drawingFactory_SDK3);
            
            GmpiDrawing::Graphics g(static_cast<GmpiDrawing_API::IMpDeviceContextExt*>(&context.sdk3Context));
            auto tf = g.GetFactory().CreateTextFormat(16, "Arial", GmpiDrawing::FontWeight::Normal);
            auto brush = g.CreateSolidColorBrush(GmpiDrawing::Color::Black);
            g.FillRectangle(0,0,40,40, brush);
            brush.SetColor(GmpiDrawing::Color::White);
            g.DrawTextU("_", tf, {0, 0, 40, 40}, brush);
            
            uint8_t* pixels = 1 + [backBuffer bitmapData];
            auto stride = [backBuffer bytesPerRow];
            int bestBrightness = 0;
            int bestRow = 1;
            
            for(int y = 0 ; y < 40 ; ++y)
            {
                if(*pixels > bestBrightness)
                {
                    bestBrightness = *pixels;
                    bestRow = y;
                }
                
                pixels += stride;
            }
            
            se::cocoa::GraphicsContext2::logicProFix = (int) (bestRow != 17 && bestRow != 33); // SD / HD (will be 18 / 35 for buggy situation)
        }
        
        // context must be disposed (via RIAA) before restoring state, because its destructor also restores state
        {
	        se::cocoa::UniversalGraphicsContext context(frame, &drawingFactory_GMPI, &drawingFactory_SDK3);

            // draw the absolute minimum.
            for( auto& r : dirtyRects.rects )
		    {
                context.pushAxisAlignedClip((const gmpi::drawing::Rect*)&r);
        
                containerView->OnRender(static_cast<GmpiDrawing_API::IMpDeviceContext*>(&context.sdk3Context));

       		    context.popAxisAlignedClip();
		    }
        }

#else
        // context must be disposed before restoring state, because it's destructor also restores state
        {
	        gmpi::cocoa::GraphicsContext2 context(frame, &drawingFactory);
        
            // JUCE standalone tends to draw over window non-client area on macOS. clip drawing.
            const auto r = [frame bounds];
            const GmpiDrawing::Rect bounds{
                (float) r.origin.x,
                (float) r.origin.y,
                (float) (r.origin.x + r.size.width),
                (float) (r.origin.y + r.size.height)
            };
            
            GmpiDrawing::Rect dirtyClipped{*dirtyRect};
            
            dirtyClipped.Intersect(bounds);

            context.PushAxisAlignedClip(&dirtyClipped);
        
        	containerView->OnRender(static_cast<GmpiDrawing_API::IMpDeviceContext*>(&context));
       		context.PopAxisAlignedClip();
       }
#endif

#if USE_BACKING_BUFFER
        [NSGraphicsContext restoreGraphicsState];
        
        // blit back buffer onto screen.
        [backBuffer drawInRect:[view bounds]]; // copes with DPI
#endif

        dirtyRects.rects.clear();
    }
    
    // Inherited via IMpUserInterfaceHost2
    int32_t MP_STDCALL pinTransmit(int32_t pinId, int32_t size, const void * data, int32_t voice = 0) override
    {
//TODO         assert(false); // not implemented.
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL createPinIterator(gmpi::IMpPinIterator** returnIterator) override
    {
//TODO         assert(false); // not implemented.
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL getHandle(int32_t & returnValue) override
    {
 //TODO        assert(false); // not implemented.
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL sendMessageToAudio(int32_t id, int32_t size, const void * messageData) override
    {
 //TODO        assert(false); // not implemented.
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL ClearResourceUris() override
    {
//TODO         assert(false); // not implemented.
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL RegisterResourceUri(const char * resourceName, const char * resourceType, gmpi::IString* returnString) override
    {
//TODO         assert(false); // not implemented.
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL OpenUri(const char * fullUri, gmpi::IProtectedFile2** returnStream) override
    {
//TODO         assert(false); // not implemented.
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL FindResourceU(const char * resourceName, const char * resourceType, gmpi::IString* returnString) override
    {
//TODO         assert(false); // not implemented.
        return gmpi::MP_FAIL;
    }
    
    int32_t MP_STDCALL LoadPresetFile_DEPRECATED(const char* presetFilePath) override
    {
        //TODO         assert(false); // not implemented.
        return gmpi::MP_FAIL;
    }

    // IDrawingHost
    gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory) override
    {
        *returnFactory = &drawingFactory_GMPI;
        return gmpi::ReturnCode::Ok;
    }
	void invalidateRect(const gmpi::drawing::Rect* invalidRect) override
    {
        invalidateRect((const GmpiDrawing_API::MP1_RECT*) invalidRect);
    }
	float getRasterizationScale() override // DPI scaling
    {
        return [[view window] backingScaleFactor];
    }
    
    // IDialogHost
    gmpi::ReturnCode createTextEdit   (const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit) override {return gmpi::ReturnCode::NoSupport;}
    gmpi::ReturnCode createPopupMenu  (const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) override {return gmpi::ReturnCode::NoSupport;}
    gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener) override
    {
        *returnKeyListener = new GMPI_MAC_KeyListener(view, r);
        return gmpi::ReturnCode::NoSupport;
    }
    gmpi::ReturnCode createFileDialog (int32_t dialogType, gmpi::api::IUnknown** returnDialog) override {return gmpi::ReturnCode::NoSupport;}
    gmpi::ReturnCode createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override {return gmpi::ReturnCode::NoSupport;}

    // IMpGraphicsHost
    void MP_STDCALL invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect) override
    {
        if(invalidRect)
        {
            dirtyRects.rects.push_back(
                {
                    floorf(invalidRect->left),
                    floorf(invalidRect->top),
                    ceilf(invalidRect->right),
                    ceilf(invalidRect->bottom)
                });

//            [view setNeedsDisplayInRect:NSMakeRect (invalidRect->left, invalidRect->top, invalidRect->right - invalidRect->left, invalidRect->bottom - invalidRect->top)];
            
            [view setNeedsDisplayInRect: GmpiGuiHosting::gmpiRectToViewRect(view.bounds, dirtyRects.rects.back())];
        }
        else
        {
            dirtyRects.rects.push_back(
                {
                    0.0f,
                    0.0f,
                    (float) (std::numeric_limits<int32_t>::max) (),
                    (float) (std::numeric_limits<int32_t>::max) ()
                });
                
            [view setNeedsDisplay:YES];
        }
    }
    void MP_STDCALL invalidateMeasure() override
    {
//TODO        assert(false); // not implemented.
    }
    int32_t MP_STDCALL setCapture(void) override
    {
        mouseCaptured = 1;
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL getCapture(int32_t & returnValue) override
    {
        returnValue = mouseCaptured;
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL releaseCapture(void) override
    {
        mouseCaptured = 0;
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL GetDrawingFactory(GmpiDrawing_API::IMpFactory ** returnFactory) override
    {
        *returnFactory = &drawingFactory_SDK3;
        return gmpi::MP_OK;
    }
    
    int32_t MP_STDCALL createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override
    {
        *returnMenu = new GmpiGuiHosting::PlatformMenu(view, rect);
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override
    {
        currentTextEdit = new GmpiGuiHosting::PlatformTextEntry(this, view, rect);
        *returnTextEdit = currentTextEdit;
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override
    {
        *returnFileDialog = new GmpiGuiHosting::PlatformFileDialog(dialogType, view);
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog) override
    {
        *returnDialog = new GmpiGuiHosting::PlatformOkCancelDialog(dialogType, view);
        return gmpi::MP_OK;
    }
    
    // IUnknown methods
    int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
    {
        if (gmpi::MpGuidEqual(&iid, (const gmpi::MpGuid*)&gmpi::api::IDrawingHost::guid))
        {
            // important to cast to correct vtable (this has 2 vtables) before reinterpret cast
            *returnInterface = reinterpret_cast<void*>(static_cast<gmpi::api::IDrawingHost*>(this));
            addRef();
            return gmpi::MP_OK;
        }
        if (gmpi::MpGuidEqual(&iid, (const gmpi::MpGuid*)&gmpi::api::IDialogHost::guid))
        {
            // important to cast to correct vtable (this has 2 vtables) before reinterpret cast
            *returnInterface = reinterpret_cast<void*>(static_cast<gmpi::api::IDialogHost*>(this));
            addRef();
            return gmpi::MP_OK;
        }
        if (iid == gmpi::MP_IID_UI_HOST2)
        {
            // important to cast to correct vtable (ug_plugin3 has 2 vtables) before reinterpret cast
            *returnInterface = reinterpret_cast<void*>(static_cast<IMpUserInterfaceHost2*>(this));
            addRef();
            return gmpi::MP_OK;
        }
        
        if (iid == gmpi_gui::SE_IID_GRAPHICS_HOST || iid == gmpi_gui::SE_IID_GRAPHICS_HOST_BASE || iid == gmpi::MP_IID_UNKNOWN)
        {
            // important to cast to correct vtable (ug_plugin3 has 2 vtables) before reinterpret cast
            *returnInterface = reinterpret_cast<void*>(static_cast<IMpGraphicsHost*>(this));
            addRef();
            return gmpi::MP_OK;
        }
        
        *returnInterface = 0;
        return gmpi::MP_NOSUPPORT;
    }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        *returnInterface = {};
        GMPI_QUERYINTERFACE(gmpi::api::IDrawingHost);
        GMPI_QUERYINTERFACE(gmpi::api::IDialogHost);
        GMPI_QUERYINTERFACE(gmpi_gui::legacy::IMpGraphicsHost);
        GMPI_QUERYINTERFACE(gmpi::legacy::IMpUserInterfaceHost2);
        return gmpi::ReturnCode::NoSupport;
    }
    
    void removeTextEdit()
    {
        if(!currentTextEdit)
            return;
        
        currentTextEdit->CallbackFromCocoa(nil);
    }
    
    void onTextEditRemoved() override
    {
        currentTextEdit = nullptr;
    }
    void initBackingBitmap()
    {
        NSSize logicalsize = view.frame.size;
        NSSize pysicalsize = [view convertRectToBacking:[view bounds]].size;

        // kCGColorSpaceGenericRGBLinear - middle gray is darker, blend seems correct.
        // kCGColorSpaceExtendedLinearSRGB, kCGColorSpaceLinearDisplayP3 - same
        // kCGColorSpaceGenericRGB - actually sRGB
        // kCGColorSpaceExtendedLinearSRGB - draws too dark after initial paint.
        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceLinearSRGB);
        
        NSColorSpace *linearRGBColorSpace = [[NSColorSpace alloc] initWithCGColorSpace:colorSpace];

        NSBitmapImageRep* imagerep1 = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
           pixelsWide:pysicalsize.width
           pixelsHigh:pysicalsize.height
           bitsPerSample:16 //8     // 1, 2, 4, 8, 12, or 16.
           samplesPerPixel:3
           hasAlpha:NO
           isPlanar:NO
           colorSpaceName: NSCalibratedRGBColorSpace // makes no difference if we retag it later anyhow.
           bitmapFormat: NSBitmapFormatFloatingPointSamples //NSAlphaFirstBitmapFormat
           bytesPerRow:0    // 0 = don't care  800 * 4
           bitsPerPixel:64 ];
        
        backBuffer = [imagerep1 bitmapImageRepByRetaggingWithColorSpace:linearRGBColorSpace];
        [backBuffer setSize: logicalsize]; // Communicates DPI

        [backBuffer retain]; // else AU2 crashes during render.

        // Release the resources
        CGColorSpaceRelease(colorSpace);
    }
    
    void onResize()
    {
        initBackingBitmap();
        
        NSSize logicalsize = view.frame.size;
        GmpiDrawing_API::MP1_RECT finalRect{0,0, (float) logicalsize.width, (float) logicalsize.height};
        containerView->arrange(finalRect);
    }
    
    GMPI_REFCOUNT_NO_DELETE;
};

GmpiDrawing::Point se_mouseToGmpi(NSView* view, NSEvent* theEvent)
{
    NSPoint localPoint = [view convertPoint: [theEvent locationInWindow] fromView: nil];
    
#if USE_BACKING_BUFFER
    localPoint.y = view.bounds.origin.y + view.bounds.size.height - localPoint.y;
#endif
    
    GmpiDrawing::Point p(localPoint.x, localPoint.y);
    return p;
}

#define SYNTHEDIT_PLUGIN_COCOA_NSVIEW_WRAPPER_CLASSNAME SE_MAKE_CLASSNAME(Cocoa_NSViewWrapperForAU)

//--------------------------------------------------------------------------------------------------------------
@interface SYNTHEDIT_PLUGIN_COCOA_NSVIEW_WRAPPER_CLASSNAME : NSView {
    //--------------------------------------------------------------------------------------------------------------
    DrawingFrameCocoa drawingFrame;
    NSTrackingArea* trackingArea;
    NSTimer* timer;
    int toolTipTimer;
    bool toolTipShown;
    GmpiDrawing::Point mousePos;
}

- (id) initWithController: (class IGuiHost2*) _editController preferredSize: (NSSize) size;
- (void)drawRect:(NSRect)dirtyRect;
- (void)onTimer: (NSTimer*) t;

@end

//--------------------------------------------------------------------------------------------------------------
// SMTG_AU_PLUGIN_NAMESPACE (SMTGAUPluginCocoaView)
//--------------------------------------------------------------------------------------------------------------

#if defined(SE_TARGET_AU)

//--------------------------------------------------------------------------------------------------------------
@implementation SYNTHEDIT_PLUGIN_COCOA_VIEW_CLASSNAME


//--------------------------------------------------------------------------------------------------------------
- (unsigned) interfaceVersion
{
    return 0;
}

//--------------------------------------------------------------------------------------------------------------
- (NSString *) description
{
    return @"Cocoa View";
}

//--------------------------------------------------------------------------------------------------------------

- (NSView *)uiViewForAudioUnit:(AudioUnit)inAU withSize:(NSSize)inPreferredSize
{
    class ausdk::AUBase* editController = {};
    UInt32 size = sizeof (editController);
    if (AudioUnitGetProperty (inAU, 64000, kAudioUnitScope_Global, 0, &editController, &size) != noErr)
        return nil;
    
    return [[[SYNTHEDIT_PLUGIN_COCOA_NSVIEW_WRAPPER_CLASSNAME alloc] initWithController:dynamic_cast<IGuiHost2*>(editController) preferredSize:inPreferredSize] autorelease];

//    return [[[SeTestView alloc] initWithController:dynamic_cast<IGuiHost2*>(editController) preferredSize:inPreferredSize] autorelease];
}

@end

#endif


//--------------------------------------------------------------------------------------------------------------
@implementation SYNTHEDIT_PLUGIN_COCOA_NSVIEW_WRAPPER_CLASSNAME

//--------------------------------------------------------------------------------------------------------------
- (id) initWithController: (class IGuiHost2*) _editController preferredSize: (NSSize) size_unused
{
    Json::Value document_json;
    Json::Reader r;
    r.parse(BundleInfo::instance()->getResource("gui.se.json"), document_json);
    auto& gui_json = document_json["gui"];
    int width = gui_json["width"].asInt();
    int height = gui_json["height"].asInt();

    self = [super initWithFrame: NSMakeRect (0, 0, width, height)];
    if (self)
    {
        self->trackingArea = [NSTrackingArea alloc];
        [self->trackingArea initWithRect:NSZeroRect options:(NSTrackingMouseEnteredAndExited | NSTrackingInVisibleRect | NSTrackingMouseMoved| NSTrackingActiveAlways) owner:self userInfo:nil];
        [self addTrackingArea:self->trackingArea ];
        
        drawingFrame.view = self;
        auto presenter = new JsonDocPresenter(_editController);
        drawingFrame.Init(presenter, _editController, CF_PANEL_VIEW);
        presenter->RefreshView();
        
        timer = [NSTimer scheduledTimerWithTimeInterval:0.1 target:self selector:@selector(onTimer:) userInfo:nil repeats:YES ];
    }
    return self;
}

// Opt-in to notification that mouse entered window.
- (void)viewDidMoveToWindow {
     [super viewDidMoveToWindow];

    auto window = [self window];
//    if(window)
//    {
//        drawingFrame.drawingFactory.setBestColorSpace();//window);
//    }
}

- (void) removeFromSuperview
{
     [super removeFromSuperview];
     
   // Editor is closing
    [self onClose];
}

-(void)onClose
{
/* don't seem nesc

    if( trackingArea )
    {
        [self removeTrackingArea:trackingArea];
        trackingArea = nil;
    }
 */
    if( trackingArea )
    {
 //       _RPT0(0, "onClose. Removing trackingArea\n");
        [trackingArea release];
        trackingArea = nil;
    }
    
    // timer will retain NSView, so need to manually stop timer right before we release this view
    if( timer )
    {
        [self->timer invalidate];
        self->timer = nil;
    }
    
    drawingFrame.DeInit();
    drawingFrame.view = nil;
}

- (void)drawRect:(NSRect)dirtyRect
{
#if !USE_BACKING_BUFFER
    GmpiDrawing::Rect r(dirtyRect.origin.x, dirtyRect.origin.y, dirtyRect.origin.x + dirtyRect.size.width, dirtyRect.origin.y + dirtyRect.size.height);
#endif
    
    drawingFrame.OnRender(self /*, &r*/);
    
#ifdef _DEBUG
    {
        // write colorspace to window in debug mode
        auto window = [self window];
        auto windowColorspace = [window colorSpace];
        auto desc = [windowColorspace debugDescription];
        
        [desc drawAtPoint:CGPointMake(110,0) withAttributes:nil];
        
        const auto bpp = NSBitsPerSampleFromDepth( [window depthLimit] );
        NSString *str = [@(bpp) stringValue];
        
        [str drawAtPoint:CGPointMake(0,0) withAttributes:nil];
        
        if([window canRepresentDisplayGamut:NSDisplayGamutP3])
        {
            [@"P3:YES" drawAtPoint:CGPointMake(320,0) withAttributes:nil];
        }
        else{
            [@"P3:NO" drawAtPoint:CGPointMake(320,0) withAttributes:nil];
        }
    }
    
#endif
 }

//--------------------------------------------------------------------------------------------------------------
- (void) setFrame: (NSRect) newSize
{
    [super setFrame: newSize];
    
    drawingFrame.onResize();
}

//--------------------------------------------------------------------------------------------------------------
#if !USE_BACKING_BUFFER
- (BOOL)isFlipped { return YES; }
#endif

- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }

//-------------------------------------------------------------------------------------------------------------

void ApplyKeyModifiers(int32_t& flags, NSEvent* theEvent)
{
    // <Shift> key?
    if(([theEvent modifierFlags ] & (NSEventModifierFlagShift | NSEventModifierFlagCapsLock)) != 0)
    {
        flags |= gmpi_gui_api::GG_POINTER_KEY_SHIFT;
    }
    
    if(([theEvent modifierFlags ] & NSEventModifierFlagControl /* was NSEventModifierFlagCommand*/ ) != 0)
    {
        flags |= gmpi_gui_api::GG_POINTER_KEY_CONTROL;
    }
    
    if(([theEvent modifierFlags ] & NSEventModifierFlagOption) != 0) // <Option> key.
    {
        flags |= gmpi_gui_api::GG_POINTER_KEY_ALT;
    }
}

- (void)mouseDown:(NSEvent *)theEvent
{
//test    [[self window] setColorSpace:[NSColorSpace genericRGBColorSpace]];
    // Try to close text edit
        /*
    if( [[[self window] firstResponder]isKindOfClass:[NSTextView class]])
    {
        auto textview = (NSTextView*) [[self window] firstResponder];
 //       [[textview delegate] textView:<#(nonnull NSTextView *)#> doCommandBySelector:<#(nonnull SEL)#>];
    }

        
        drawingFrame.removeTextEdit();
/ *
        auto textview = (NSTextView*) [[self window] firstResponder];
        auto del = textview.delegate;
        auto notification = [NSNotification alloc];// notification;
        [del textDidEndEditing:notification];
  //      [textview.delegate textDidEndEditing:textview];
//        [[(NSTextView*)[[self window] firstResponder] delegate] textDidEndEditing:<#(nonnull NSNotification *)#>];
//[[self window] performSelector:@selector(resignFirstResponder:) withObject:myTextField afterDelay:0.0];
 * /
    }
 */
    drawingFrame.removeTextEdit();
    
    [[self window] makeFirstResponder:self]; // take focus off any text-edit. Works but does not dimiss it.
 
    int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
    flags |= gmpi_gui_api::GG_POINTER_FLAG_NEW;
	flags |= gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON;
    
    ApplyKeyModifiers(flags, theEvent);
    
    drawingFrame.getView()->onPointerDown(flags, se_mouseToGmpi(self, theEvent));
    
 // no help to edit box   [super mouseDown:theEvent];
}

- (void)rightMouseDown:(NSEvent *)theEvent
{
    drawingFrame.removeTextEdit();
        
    int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
    flags |= gmpi_gui_api::GG_POINTER_FLAG_NEW;
    flags |= gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON;
    
    ApplyKeyModifiers(flags, theEvent);
    
    drawingFrame.getView()->onPointerDown(flags, se_mouseToGmpi(self, theEvent));
}

- (void)rightMouseUp:(NSEvent *)theEvent
{
    int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
    flags |= gmpi_gui_api::GG_POINTER_FLAG_NEW;
    flags |= gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON;
    
    ApplyKeyModifiers(flags, theEvent);
    
    drawingFrame.getView()->onPointerUp(flags, se_mouseToGmpi(self, theEvent));
}

- (void)mouseUp:(NSEvent *)theEvent {
    int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
    flags |= gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON;
    
    ApplyKeyModifiers(flags, theEvent);
    
    drawingFrame.getView()->onPointerUp(flags, se_mouseToGmpi(self, theEvent));
}

- (void)mouseDragged:(NSEvent *)theEvent {
    int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
    flags |= gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON;
    
    ApplyKeyModifiers(flags, theEvent);
    
    mousePos = se_mouseToGmpi(self, theEvent);
    drawingFrame.getView()->onPointerMove(flags, mousePos);
    
    [self ToolTipOnMouseActivity];
}

- (void)scrollWheel:(NSEvent *)theEvent {
    // Get the scroll wheel delta
    auto deltaX = theEvent.deltaX;
    auto deltaY = theEvent.deltaY;

    int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
    ApplyKeyModifiers(flags, theEvent);

    constexpr float wheelConversion = 120.0f; // on windows the wheel scrolls 120 per knotch
    if(deltaY)
    {
        drawingFrame.getView()->onMouseWheel(flags, wheelConversion * deltaY, se_mouseToGmpi(self, theEvent));
    }
    if(deltaX)
    {
        flags |= gmpi_gui_api::GG_POINTER_SCROLL_HORIZ;
        drawingFrame.getView()->onMouseWheel(flags, wheelConversion * deltaX, se_mouseToGmpi(self, theEvent));
    }
}

- (void)mouseEntered:(NSEvent *)theEvent {
    [[self window] setAcceptsMouseMovedEvents:YES];
    [[self window] makeFirstResponder:self];
}

- (void)mouseMoved:(NSEvent *)theEvent {
    
    int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
    flags |= gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON;
    
    ApplyKeyModifiers(flags, theEvent);
    
    mousePos = se_mouseToGmpi(self, theEvent);
    
    drawingFrame.getView()->onPointerMove(flags, mousePos);

    [self ToolTipOnMouseActivity];
}

- (void)mouseExited:(NSEvent *)theEvent {
    [[self window] setAcceptsMouseMovedEvents:NO];
}

- (void) onTimer: (NSTimer*) t {
    if(toolTipTimer-- == 0 && !toolTipShown)
    {
        gmpi_sdk::MpString text;
        drawingFrame.getView()->getToolTip(mousePos, &text);
        
        if(text.str().empty())
        {
            [self setToolTip:nil];
        }
        else
        {
            NSString* nsstr = [NSString stringWithCString : text.c_str() encoding : NSUTF8StringEncoding];
            [self setToolTip:nsstr];
        }
        toolTipShown = true;
    }
}

- (void)ToolTipOnMouseActivity {
    if(toolTipShown)
    {
        [self setToolTip:nil];
        toolTipShown = false;
        toolTipTimer = 2;
    }
}

@end

// without including objective-C headers, we need to create an NSView from C++.
// here is the function here to return the view, using void* as return type.
void* createNativeView(class IGuiHost2* controller, int width, int height)
{
    NSSize inPreferredSize{(CGFloat)width, (CGFloat)height};
    
    return (void*) [[SYNTHEDIT_PLUGIN_COCOA_NSVIEW_WRAPPER_CLASSNAME alloc] initWithController:controller preferredSize:inPreferredSize];
}

// VST3 version
void* createNativeView(void* parent, class IGuiHost2* controller, int width, int height)
{
    NSSize inPreferredSize{(CGFloat)width, (CGFloat)height};
    NSView* native =
    [[SYNTHEDIT_PLUGIN_COCOA_NSVIEW_WRAPPER_CLASSNAME alloc] initWithController:controller preferredSize:inPreferredSize];
    
    NSView* parentView = (NSView*) parent;
    [parentView addSubview:native];
    
    return (void*) native;
}

void onCloseNativeView(void* view_ptr)
{
    auto view = (SYNTHEDIT_PLUGIN_COCOA_NSVIEW_WRAPPER_CLASSNAME*) view_ptr;
    [view onClose];
}

/* duplicates GMPI one

void resizeNativeView(void* view_ptr, int width, int height)
{
    auto view = (SYNTHEDIT_PLUGIN_COCOA_NSVIEW_WRAPPER_CLASSNAME*) view_ptr;
    auto r = [view frame];
    r.size.width = width;
    r.size.height = height;
    [view setFrame:r];
}
*/
