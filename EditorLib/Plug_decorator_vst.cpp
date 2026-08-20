 // stdafx is needed as when using both MFC (std::wstring) and the std C library else get multiply defined symbols (operator new etc) net PRB: LNK2005
#include "./conversion.h"
#include "./Plug_decorator_vst.h"
#include "./Notify_msg.h"
#include "./CUG.h"
#include "./SynthEditDocBase.h"

std::wstring Plug_decorator_vst::getName(IPlug* self)
{
	return L"*";
}

void Plug_decorator_vst::Import(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	getPlugDescription()->Import(self, xml, targetType);
}

void Plug_decorator_vst::Export(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType)
{
	getPlugDescription()->Export(self, xml, targetType);
}

