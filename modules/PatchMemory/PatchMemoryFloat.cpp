#include "./PatchMemoryFloat.h"

SE_DECLARE_INIT_STATIC_FILE(PatchMemoryFloat)
SE_DECLARE_INIT_STATIC_FILE(PatchMemoryFloatOut)
SE_DECLARE_INIT_STATIC_FILE(PatchMemoryFloatLatchable)

REGISTER_PLUGIN( PatchMemoryFloat, L"SE PatchMemory Float" );
REGISTER_PLUGIN( PatchMemoryFloat, L"SE PatchMemory Float Out" );
REGISTER_PLUGIN( PatchMemoryFloat, L"SE PatchMemory Float Out B2" );

PatchMemoryFloat::PatchMemoryFloat(IMpUnknown* host) : MpBase(host)
{
	initializePin( 0, pinValueIn );
	initializePin( 1, pinValueOut );
}

void PatchMemoryFloat::onSetPins()  // one or more pins_ updated.  Check pin update flags to determin which ones.
{
	if( pinValueIn.isUpdated() )
	{
		pinValueOut = pinValueIn;
	}
}

class PatchMemoryFloatLatchable : public MpBase2
{
public:
	PatchMemoryFloatLatchable()
	{
		initializePin(pinValueHostIn);
		initializePin(pinValueHostOut);
		initializePin(pinLatchInput);
		initializePin(pinValueIn);
		initializePin(pinValueOut);
	}

	void onSetPins() override
	{
		float value;
		if (pinLatchInput.getValue())
		{
			value = pinValueIn.getValue();
//#ifdef _DEBUG
//		_RPTN(0, "[%5d] PM IO -> %f IN\n", host.getHandle(), value);
//#endif		
		}
		else
		{
			value = pinValueHostIn.getValue();
//#ifdef _DEBUG
//			if (444539279 != host.getHandle())
//			{
//				_RPTN(0, "[%5d] PM IO -> %f OUT\n", host.getHandle(), value);
//			}
//#endif		
		}

		pinValueOut = value;
		pinValueHostOut = value;

		// When updating patch from DSP, SE does not reflect the value back to pinValueHostIn automatically, fake it.
		pinValueHostIn.setValueRaw(sizeof(value), &value);
	}

private:
	FloatInPin pinValueHostIn;
	FloatOutPin pinValueHostOut;
	BoolInPin pinLatchInput;
	FloatInPin pinValueIn;
	FloatOutPin pinValueOut;
};

namespace
{
	auto r = sesdk::Register<PatchMemoryFloatLatchable>::withId(L"SE PatchMemory Float Latchable");
}
