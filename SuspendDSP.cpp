
#include "./SuspendDSP.h"
#include "Application.h"

SuspendDSP::SuspendDSP(ApplicationBase* p_app)
{
	p_app->invalidateDsp();
#if 0
	m_app = dynamic_cast<CSynthEditAppBase*>(p_app);

	if (!m_app)
	{
		throw std::runtime_error("SuspendDSP: TODO support this in TIDE");
		return;
	}
	//if (hard) // restart soundcard and DSP
	//{
	//	// restart synth engine if nesc
	//	if (p_app->SynthRunning())
	//	{
	//		m_app = p_app;
	//		m_app->OnRunStop();
	//	}
	//}
	//else // restart only DSP
	//{
	m_app->dspDirty = true;
	//}
#endif
}

SuspendDSP::~SuspendDSP()
{
	// needed?
	//if( m_app )
	//{
	//	m_app->OnRunPlay();
	//}
}
