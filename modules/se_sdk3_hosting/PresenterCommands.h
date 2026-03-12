#pragma once

/*
#include "PresenterCommands.h"
*/

enum class PresenterCommand { Delete, SelectAll, Undo, Redo, Cut, Copy, Paste, ToFront, ToBack, Lock, Contain, UnContain, Open, RefreshView, UnloadView, ImportPrefab, PickupLineFrom, PickupLineTo, CancelPickupLine };

enum {
	PinHighlightFlag_None               = 0,
	PinHighlightFlag_Feedback           = 1 << 0,
	PinHighlightFlag_UiFeedback         = 1 << 1,
	PinHighlightFlag_Emphasise          = 1 << 2,
	PinHighlightFlag_EmphasiseMomentary = 1 << 3,
};