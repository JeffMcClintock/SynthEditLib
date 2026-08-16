#pragma once

// BACKLOG C11. A minimal hook so public-repo code can ask "should a
// paid-tier feature be grayed out?" without depending on any private app's
// concrete type or naming its licensing scheme. Before this, MfcDocPresenter.cpp
// (public) declared `extern SynthEditApp*` and called two Moonbase-named
// methods directly, which put a licence-gate call site -- two method names
// and the existence of the gate -- in the public repo. This narrows that to
// a generic interface: only the concept "gated feature, currently licensed
// or not" is visible here, not the private class or its naming.
//
// An app with no gated features -- TIDE, per PLAN's "Price and funding": TIDE
// is free -- returns nullptr from GetLicenseState() rather than implementing
// this at all. See SynthEditSem/TideAppStubs.cpp.
class ILicenseState
{
public:
	virtual ~ILicenseState() = default;

	// True if this build has a paid tier to enforce at all.
	virtual bool hasGatedFeatures() const = 0;
	// True if the current license entitles the user to the gated features.
	// Not const: the underlying check can refresh cached activation state.
	virtual bool isLicensed() = 0;
};

// Returns the current process's license state, or nullptr if this app has
// none. Defined once per app (SynthEdit2/SynthEditApp.cpp for the desktop
// app and SynthEditCL, which compile the same file; TideAppStubs.cpp for
// TIDE), the same pattern as the other app-supplied symbols in this file.
ILicenseState* GetLicenseState();
