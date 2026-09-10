#include "./LatencyTest.h"

REGISTER_PLUGIN2 ( LatencyTest, L"SE Latency Test" );
REGISTER_PLUGIN2(LatencyTest2, L"SE Latency Test2");

LatencyTest::LatencyTest( )
{
	// Register pins.
	initializePin( pinSignalIn );
	initializePin( pinSignalOut );

	// Set processing method.
	setSubProcess(&LatencyTest::subProcess);
}

void LatencyTest::subProcess( int sampleFrames )
{
	// get pointers to in/output buffers.
	const float* signalIn	= getBuffer(pinSignalIn);
	float* signalOut= getBuffer(pinSignalOut);

	for( int s = sampleFrames; s > 0; --s )
	{
		*signalOut = *signalIn;

		// Increment buffer pointers.
		++signalIn;
		++signalOut;
	}
}

void LatencyTest::onSetPins()
{
	// Set state of output audio pins.
	pinSignalOut.setStreaming(pinSignalIn.isStreaming());
}

class OsRate : public MpBase2
{
public:
	OsRate()
	{
		initializePin(pinHostControl);
		initializePin(pinValueOut);
	}
	virtual void onSetPins() override
	{
		_RPTN(0, "OsRate::onSetPins(%d)\n", pinHostControl.getValue());
		pinValueOut = pinHostControl;
	}

protected:
	IntInPin pinHostControl;
	IntOutPin pinValueOut;
};

REGISTER_PLUGIN2(OsRate, L"SE OS RATE");
