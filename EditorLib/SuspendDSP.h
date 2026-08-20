#pragma once

// causes the DSP to receive a fresh copy of the document.
class SuspendDSP
{
public:
	SuspendDSP(class ApplicationBase* p_app);
	~SuspendDSP();

//private:
//	class CSynthEditAppBase* m_app{};
};
