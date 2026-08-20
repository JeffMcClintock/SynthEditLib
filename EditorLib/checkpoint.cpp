#include "checkpoint.h"
#include "SynthEditDocBase.h"
#include "Application.h"
#include "CContainer.h"

CheckpointFullSerialise::CheckpointFullSerialise(CSynthEditDocBase* mfc_document, const std::wstring& p_description)
{
	assert(mfc_document);

	description = p_description;

	// store entire document to memory file
	mfc_document->Snapshot(m_document);
}

void CheckpointFullSerialise::Undo(CSynthEditDocBase*)
{
	assert(false); // nope. got to step back to previous state, not restore from this one.
}

void CheckpointFullSerialise::Redo(CSynthEditDocBase* mfc_document)
{
	mfc_document->DeleteContents();
	mfc_document->SetModified();  // dirty during de-serialize
	mfc_document->ImportModules(m_document.RootElement(), SAT_SYNTHEDIT_DOCUMENT);

	mfc_document->MasterContainer->OpenViews();

	auto container = dynamic_cast<CContainer*>(mfc_document->uniqueIdDatabase.HandleToObjectWithNull(SelectedViewHandle));
	if (container)
		mfc_document->OpenView(container, SelectedViewType);
}
