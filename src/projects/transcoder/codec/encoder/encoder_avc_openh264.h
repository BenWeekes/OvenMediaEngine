//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../transcoder_encoder.h"

class EncoderAVCxOpenH264 : public TranscodeEncoder
{
public:
	EncoderAVCxOpenH264(const info::Stream &stream_info)
		: TranscodeEncoder(stream_info)
	{
	}

	~EncoderAVCxOpenH264();

	AVCodecID GetCodecID() const noexcept override
	{
		return AV_CODEC_ID_H264;
	}

	int GetSupportedFormat() const noexcept override
	{
		return AV_PIX_FMT_YUV420P;
	}

	cmn::BitstreamFormat GetBitstreamFormat() const noexcept override
	{
		return cmn::BitstreamFormat::H264_ANNEXB;
	}

	bool Configure(std::shared_ptr<MediaTrack> context) override;

	void CodecThread() override;

	void OnMixerAppFrame(const std::shared_ptr<const MediaFrame>& frame) override;

private:
   bool CreateContext(AVPixelFormat format, const uint32_t& srcWidth, const uint32_t& srcHeight,
                   const uint32_t& targetWidth, const uint32_t& targetHeight);

	bool ScaleFrame(std::shared_ptr<const MediaFrame> srcFrame, AVFrame* dstFrame);
	void MixFrames(const std::shared_ptr<const MediaFrame>& dstFrame,
	                        const std::shared_ptr<const MediaFrame>& Frame);

    void CopyFrameData(const AVFrame* dstFrame, const AVFrame* srcFrame,
	                    const uint32_t& width, const uint32_t& height, const uint8_t& index);

	AVFrame* CreateAvFrame(AVPixelFormat format,
                     const uint32_t& width, const uint32_t& height);

private:
	bool SetCodecParams() override;

	std::queue<std::shared_ptr<const MediaFrame>>  _mixerFrames;
	SwsContext* _scaleContext{nullptr};
	AVFrame*    _avMixerFrame{nullptr};
};
