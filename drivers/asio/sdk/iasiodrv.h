#include "asiosys.h"
#include "asio.h"

/* Forward Declarations */ 

#ifndef __ASIODRIVER_FWD_DEFINED__
#define __ASIODRIVER_FWD_DEFINED__
typedef interface IASIO IASIO;
#endif 	/* __ASIODRIVER_FWD_DEFINED__ */

interface IASIO : public IUnknown
{

	virtual ASIOBool ASIO_CALL init(void *sysHandle) = 0;
	virtual void ASIO_CALL getDriverName(char *name) = 0;	
	virtual long ASIO_CALL getDriverVersion() = 0;
	virtual void ASIO_CALL getErrorMessage(char *string) = 0;	
	virtual ASIOError ASIO_CALL start() = 0;
	virtual ASIOError ASIO_CALL stop() = 0;
	virtual ASIOError ASIO_CALL getChannels(long *numInputChannels, long *numOutputChannels) = 0;
	virtual ASIOError ASIO_CALL getLatencies(long *inputLatency, long *outputLatency) = 0;
	virtual ASIOError ASIO_CALL getBufferSize(long *minSize, long *maxSize,
		long *preferredSize, long *granularity) = 0;
	virtual ASIOError ASIO_CALL canSampleRate(ASIOSampleRate sampleRate) = 0;
	virtual ASIOError ASIO_CALL getSampleRate(ASIOSampleRate *sampleRate) = 0;
	virtual ASIOError ASIO_CALL setSampleRate(ASIOSampleRate sampleRate) = 0;
	virtual ASIOError ASIO_CALL getClockSources(ASIOClockSource *clocks, long *numSources) = 0;
	virtual ASIOError ASIO_CALL setClockSource(long reference) = 0;
	virtual ASIOError ASIO_CALL getSamplePosition(ASIOSamples *sPos, ASIOTimeStamp *tStamp) = 0;
	virtual ASIOError ASIO_CALL getChannelInfo(ASIOChannelInfo *info) = 0;
	virtual ASIOError ASIO_CALL createBuffers(ASIOBufferInfo *bufferInfos, long numChannels,
		long bufferSize, ASIOCallbacks *callbacks) = 0;
	virtual ASIOError ASIO_CALL disposeBuffers() = 0;
	virtual ASIOError ASIO_CALL controlPanel() = 0;
	virtual ASIOError ASIO_CALL future(long selector,void *opt) = 0;
	virtual ASIOError ASIO_CALL outputReady() = 0;
};
