//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/info/stream.h"
#include "codec/codec_base.h"

class TranscodeEncoder : public TranscodeBase<MediaFrame, MediaPacket>
{
public:
	typedef std::function<void(int32_t, std::shared_ptr<MediaPacket>)> CompleteHandler;

public:
	TranscodeEncoder(info::Stream stream_info);
	~TranscodeEncoder() override;

	static std::shared_ptr<TranscodeEncoder> Create(int32_t encoder_id, const info::Stream &info, std::shared_ptr<MediaTrack> output_track, CompleteHandler complete_handler);
	void SetEncoderId(int32_t encoder_id);

	virtual int GetSupportedFormat() const noexcept = 0;
	virtual cmn::BitstreamFormat GetBitstreamFormat() const noexcept = 0;
	bool Configure(std::shared_ptr<MediaTrack> output_track) override;

	void SendBuffer(std::shared_ptr<const MediaFrame> frame) override;
	void SendOutputBuffer(std::shared_ptr<MediaPacket> packet);

	std::shared_ptr<MediaTrack> &GetRefTrack();

	virtual void CodecThread() = 0;

	virtual void Stop();

	cmn::Timebase GetTimebase() const;


public:

	void SetCompleteHandler(CompleteHandler complete_handler)
	{
		_complete_handler = move(complete_handler);
	}

	void OnMixerAppFrame(const std::shared_ptr<const MediaFrame>& frame);

private:
	virtual bool SetCodecParams() = 0;

	bool CreateContext(AVPixelFormat format, const uint32_t& srcWidth, const uint32_t& srcHeight,
                   const uint32_t& targetWidth, const uint32_t& targetHeight);

	bool ScaleFrame(std::shared_ptr<const MediaFrame> srcFrame, AVFrame* dstFrame);
	void MixFrames(const std::shared_ptr<const MediaFrame>& dstFrame,
	                        const std::shared_ptr<const MediaFrame>& Frame);

    void CopyFrameData(const AVFrame* dstFrame, const AVFrame* srcFrame,
	                    const uint32_t& width, const uint32_t& height, const uint8_t& index);

	AVFrame* CreateAvFrame(AVPixelFormat format,
                     const uint32_t& width, const uint32_t& height);

protected:
	std::shared_ptr<MediaTrack> _track = nullptr;

	int32_t _encoder_id;

	AVCodecContext *_codec_context = nullptr;
	AVCodecParserContext *_parser = nullptr;
	AVCodecParameters *_codec_par = nullptr;

	bool _change_format = false;

	AVPacket *_packet = nullptr;
	AVFrame *_frame = nullptr;

	info::Stream _stream_info;

	bool _kill_flag = false;
	std::thread _codec_thread;

	CompleteHandler _complete_handler;

	std::queue<std::shared_ptr<const MediaFrame>>  _mixerFrames;
	SwsContext* _scaleContext{nullptr};
	AVFrame*    _avMixerFrame{nullptr};
};
