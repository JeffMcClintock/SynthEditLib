// Cocoa recreation of the editor's 'Assign Controller' dialog for plugins on macOS.
// The layout is native; all the MIDI-assignment logic (type list, CC list, packing,
// visibility) comes from AssignControllerDialogShared so it can't drift from the Windows
// version. Built for manual reference counting (this target is not ARC).

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include "AssignControllerDialogShared.h"

namespace
{

// std::wstring is UTF-32 on macOS (4-byte wchar_t); bridge via UTF-32 rather than a
// lossy char* cast.
NSString* WStringToNSString(const std::wstring& s)
{
	if (s.empty())
		return @"";

	NSString* r = [[NSString alloc] initWithBytes:s.data()
										   length:s.size() * sizeof(wchar_t)
										 encoding:NSUTF32LittleEndianStringEncoding];
	return r ? [r autorelease] : @"";
}

std::wstring NSStringToWString(NSString* s)
{
	if (s.length == 0)
		return std::wstring();

	NSData* d = [s dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
	if (!d)
		return std::wstring();

	return std::wstring(reinterpret_cast<const wchar_t*>(d.bytes), d.length / sizeof(wchar_t));
}

} // namespace

@interface SEAssignControllerDialog : NSObject
{
	NSWindow* _window;
	NSPopUpButton* _typePopup;
	NSTextField* _numberLabel;
	NSPopUpButton* _ccPopup;    // shown for CC
	NSTextField* _numberField;  // shown for RPN/NRPN
	NSTextField* _sysexLabel;
	NSTextField* _sysexField;   // shown for SYSEX
	SE2::MidiAssignment* _assignment;
}
- (instancetype)initWithAssignment:(SE2::MidiAssignment*)assignment;
- (NSWindow*)window;
@end

@implementation SEAssignControllerDialog

- (instancetype)initWithAssignment:(SE2::MidiAssignment*)assignment
{
	self = [super init];
	if (self)
	{
		_assignment = assignment;

		_window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 550, 320)
											  styleMask:NSWindowStyleMaskTitled
												backing:NSBackingStoreBuffered
												  defer:NO];
		_window.title = @"Assign Controller";
		_window.releasedWhenClosed = NO; // we own it; released in dealloc.

		[self setupUI];
		[self loadAssignment];
		[_window center];
	}
	return self;
}

- (void)dealloc
{
	[_typePopup release];
	[_numberLabel release];
	[_ccPopup release];
	[_numberField release];
	[_sysexLabel release];
	[_sysexField release];
	[_window release];
	[super dealloc];
}

- (NSWindow*)window { return _window; }

- (void)setupUI
{
	NSView* contentView = _window.contentView;
	CGFloat y = 280;

	NSTextField* typeLabel = [NSTextField labelWithString:@"Controller Type:"];
	typeLabel.frame = NSMakeRect(20, y, 150, 17);
	[contentView addSubview:typeLabel];

	_typePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(180, y - 2, 350, 26)];
	for (const auto& t : SE2::controllerTypeList())
	{
		[_typePopup addItemWithTitle:WStringToNSString(std::wstring(t.name))];
		[_typePopup lastItem].tag = t.type;
	}
	_typePopup.target = self;
	_typePopup.action = @selector(typeChanged:);
	[contentView addSubview:_typePopup];
	y -= 45;

	_numberLabel = [[NSTextField labelWithString:@"Controller Number:"] retain];
	_numberLabel.frame = NSMakeRect(20, y, 150, 17);
	[contentView addSubview:_numberLabel];

	_ccPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(180, y - 2, 350, 26)];
	for (const auto& cc : SE2::ccNameList())
	{
		[_ccPopup addItemWithTitle:WStringToNSString(cc.name)];
		[_ccPopup lastItem].tag = cc.ccNumber;
	}
	[contentView addSubview:_ccPopup];

	_numberField = [[NSTextField alloc] initWithFrame:NSMakeRect(180, y - 2, 350, 24)];
	_numberField.placeholderString = @"0-16383";
	[contentView addSubview:_numberField];
	y -= 45;

	_sysexLabel = [[NSTextField labelWithString:@"SYSEX String:"] retain];
	_sysexLabel.frame = NSMakeRect(20, y, 150, 17);
	[contentView addSubview:_sysexLabel];

	_sysexField = [[NSTextField alloc] initWithFrame:NSMakeRect(180, y - 50, 350, 44)];
	_sysexField.placeholderString = @"SYSEX bytes (hex)";
	[contentView addSubview:_sysexField];

	NSButton* okButton = [[NSButton alloc] initWithFrame:NSMakeRect(420, 12, 100, 32)];
	okButton.title = @"OK";
	okButton.bezelStyle = NSBezelStyleRounded;
	okButton.target = self;
	okButton.action = @selector(okClicked:);
	okButton.keyEquivalent = @"\r";
	[contentView addSubview:okButton];
	[okButton release];

	NSButton* cancelButton = [[NSButton alloc] initWithFrame:NSMakeRect(310, 12, 100, 32)];
	cancelButton.title = @"Cancel";
	cancelButton.bezelStyle = NSBezelStyleRounded;
	cancelButton.target = self;
	cancelButton.action = @selector(cancelClicked:);
	cancelButton.keyEquivalent = @"\033";
	[contentView addSubview:cancelButton];
	[cancelButton release];
}

- (int32_t)selectedType
{
	NSMenuItem* item = _typePopup.selectedItem;
	return item ? (int32_t)item.tag : ControllerType::None;
}

- (void)loadAssignment
{
	const auto decoded = SE2::decodeAutomation(_assignment->automation);

	const NSInteger typeIdx = [_typePopup indexOfItemWithTag:decoded.type];
	[_typePopup selectItemAtIndex:(typeIdx >= 0 ? typeIdx : 0)];

	switch (SE2::fieldForType(decoded.type))
	{
	case SE2::AssignmentField::CcList:
	{
		const NSInteger ccIdx = [_ccPopup indexOfItemWithTag:decoded.number];
		if (ccIdx >= 0)
			[_ccPopup selectItemAtIndex:ccIdx];
		break;
	}
	case SE2::AssignmentField::Number:
		_numberField.stringValue = [NSString stringWithFormat:@"%d", decoded.number];
		break;
	default:
		break;
	}

	_sysexField.stringValue = WStringToNSString(_assignment->sysex);

	[self updateVisibility];
}

- (void)updateVisibility
{
	const auto field = SE2::fieldForType([self selectedType]);
	const BOOL isCC = field == SE2::AssignmentField::CcList;
	const BOOL isNumber = field == SE2::AssignmentField::Number;
	const BOOL isSysex = field == SE2::AssignmentField::Sysex;

	_numberLabel.hidden = !(isCC || isNumber);
	_ccPopup.hidden = !isCC;
	_numberField.hidden = !isNumber;
	_sysexLabel.hidden = !isSysex;
	_sysexField.hidden = !isSysex;
}

- (void)typeChanged:(id)sender
{
	[self updateVisibility];
}

- (void)okClicked:(id)sender
{
	const int32_t type = [self selectedType];

	int32_t number = 0;
	switch (SE2::fieldForType(type))
	{
	case SE2::AssignmentField::CcList:
	{
		NSMenuItem* item = _ccPopup.selectedItem;
		number = item ? (int32_t)item.tag : 0;
		break;
	}
	case SE2::AssignmentField::Number:
		number = std::clamp((int)_numberField.intValue, 0, SE2::kMaxRpnNumber);
		break;
	default:
		break;
	}

	_assignment->automation = SE2::encodeAutomation(type, number);
	_assignment->sysex = (_sysexField.stringValue.length > 0) ? NSStringToWString(_sysexField.stringValue) : std::wstring();

	[NSApp stopModalWithCode:NSModalResponseOK];
}

- (void)cancelClicked:(id)sender
{
	[NSApp stopModalWithCode:NSModalResponseCancel];
}

@end

namespace SE2
{

bool ShowAssignControllerDialog(void* /*parentWindow*/, MidiAssignment& assignment)
{
	// A free-standing modal window (the plugin's NSView isn't needed to parent it),
	// run synchronously so the caller can apply the result inline like the Windows path.
	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

	SEAssignControllerDialog* dialog = [[SEAssignControllerDialog alloc] initWithAssignment:&assignment];
	const NSModalResponse response = [NSApp runModalForWindow:dialog.window];
	[dialog.window orderOut:nil];
	[dialog release];

	[pool release];
	return response == NSModalResponseOK;
}

} // namespace SE2
