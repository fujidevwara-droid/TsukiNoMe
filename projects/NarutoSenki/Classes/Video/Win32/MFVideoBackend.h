#pragma once
#include "../PipelineBackend.h"

// The Windows backend: Media Foundation decodes (see MFPipelineSource in the .cpp), OpenAL
// plays the sound (ALPcmSink), and PipelineBackend does everything else: threads, clocks,
// frame pacing, seeking, and the glTexSubImage2D into a plain GL texture.
//
// Pictures are converted to RGB32 by Media Foundation's video processor (so decoding is
// on the CPU); the sound is decoded to 16-bit mono or stereo PCM.
class MFVideoBackend : public PipelineBackend
{
public:
	MFVideoBackend();
	virtual ~MFVideoBackend();

	virtual bool open(const VideoSource &source, int64_t offset, int64_t length, VideoInfo &info, std::string &error);
	virtual void close();

private:
	bool _mfStarted = false;
	bool _comInitialized = false;
};
