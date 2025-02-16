//==============================================================================
//
//  Transcoder
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "encoder_avc_openh264.h"

#include <unistd.h>

#include "../../transcoder_private.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>

#include <libavutil/common.h>
#include <libavutil/pixdesc.h>
#include <libavformat/avformat.h>
}

//FIXME: how should we pass these params?
//we will use 180P
constexpr uint32_t mixerFrameHeight=180;

//this will be updated later when we know input frame resolution
uint32_t           mixerFrameWidth=320;


EncoderAVCxOpenH264::~EncoderAVCxOpenH264()
{
	if(_avMixerFrame!=nullptr)
	{
       	OV_SAFE_FUNC(_avMixerFrame, nullptr, ::av_frame_free, &);
	}
}
bool EncoderAVCxOpenH264::SetCodecParams()
{
	_codec_context->framerate = ::av_d2q((GetRefTrack()->GetFrameRate() > 0) ? GetRefTrack()->GetFrameRate() : GetRefTrack()->GetEstimateFrameRate(), AV_TIME_BASE);
	_codec_context->bit_rate = _codec_context->rc_min_rate = _codec_context->rc_max_rate = GetRefTrack()->GetBitrate();
	_codec_context->sample_aspect_ratio = ::av_make_q(1, 1);
	_codec_context->ticks_per_frame = 2;
	_codec_context->time_base = ::av_inv_q(::av_mul_q(_codec_context->framerate, (AVRational){_codec_context->ticks_per_frame, 1}));
	_codec_context->pix_fmt = (AVPixelFormat)GetSupportedFormat();
	_codec_context->width = GetRefTrack()->GetWidth();
	_codec_context->height = GetRefTrack()->GetHeight();

	// Set KeyFrame Interval
	_codec_context->gop_size = (GetRefTrack()->GetKeyFrameInterval() == 0) ? (_codec_context->framerate.num / _codec_context->framerate.den) : GetRefTrack()->GetKeyFrameInterval();

	// -1(Default) => FFMIN(FFMAX(4, av_cpu_count() / 3), 8) 
	// 0 => Auto
	// >1 => Set
	_codec_context->thread_count = GetRefTrack()->GetThreadCount() < 0 ? FFMIN(FFMAX(4, av_cpu_count() / 3), 8) : GetRefTrack()->GetThreadCount();
	_codec_context->slices = _codec_context->thread_count;

	::av_opt_set(_codec_context->priv_data, "coder", "default", 0);

	// Use the high profile to remove this log.
	//  'Warning:bEnableFrameSkip = 0,bitrate can't be controlled for RC_QUALITY_MODE,RC_BITRATE_MODE and RC_TIMESTAMP_MODE without enabling skip frame'
	::av_opt_set(_codec_context->priv_data, "allow_skip_frames", "false", 0);

	// Profile
	auto profile = GetRefTrack()->GetProfile();
	if (profile.IsEmpty() == true)
	{
		::av_opt_set(_codec_context->priv_data, "profile", "constrained_baseline", 0);
	}
	else
	{
		if (profile == "baseline")
		{
			::av_opt_set(_codec_context->priv_data, "profile", "constrained_baseline", 0);
		}
		else if (profile == "high")
		{
			::av_opt_set(_codec_context->priv_data, "profile", "high", 0);
		}
		else
		{
			 if (profile == "main") 
				logtw("OpenH264 does not support the main profile. The main profile is changed to the baseline profile.");
			else
				logtw("This is an unknown profile. change to the default(baseline) profile.");

			::av_opt_set(_codec_context->priv_data, "profile", "constrained_baseline", 0);
		}
	}

	// Loop Filter
	::av_opt_set_int(_codec_context->priv_data, "loopfilter", 1, 0);

	// Preset
	auto preset = GetRefTrack()->GetPreset().LowerCaseString();
	if (preset.IsEmpty() == true)
	{
		::av_opt_set(_codec_context->priv_data, "rc_mode", "bitrate", 0);
	}
	else
	{
		logtd("If the preset is used in the openh264 codec, constant bitrate is not supported");

		::av_opt_set(_codec_context->priv_data, "rc_mode", "quality", 0);

		if (preset == "slower")
		{
			_codec_context->qmin = 10;
			_codec_context->qmax = 39;
		}
		else if (preset == "slow")
		{
			_codec_context->qmin = 16;
			_codec_context->qmax = 45;
		}
		else if (preset == "medium")
		{
			_codec_context->qmin = 24;
			_codec_context->qmax = 51;
		}		
		else if (preset == "fast")
		{
			_codec_context->qmin = 32;
			_codec_context->qmax = 51;
		}
		else if (preset == "faster")
		{
			_codec_context->qmin = 40;
			_codec_context->qmax = 51;
		}		
		else{
			logtw("Unknown preset: %s", preset.CStr());
		}
	}

	return true;
}

bool EncoderAVCxOpenH264::Configure(std::shared_ptr<MediaTrack> context)
{
	if (TranscodeEncoder::Configure(context) == false)
	{
		return false;
	}
	auto codec_id = GetCodecID();

	const AVCodec *codec = ::avcodec_find_encoder_by_name("libopenh264");
	if (codec == nullptr)
	{
		logte("Could not find encoder: %d (%s)", codec_id, ::avcodec_get_name(codec_id));
		return false;
	}

	_codec_context = ::avcodec_alloc_context3(codec);
	if (_codec_context == nullptr)
	{
		logte("Could not allocate codec context for %s (%d)", ::avcodec_get_name(codec_id), codec_id);
		return false;
	}

	if (SetCodecParams() == false)
	{
		logte("Could not set codec parameters for %s (%d)", ::avcodec_get_name(codec_id), codec_id);
		return false;
	}

	if (::avcodec_open2(_codec_context, codec, nullptr) < 0)
	{
		logte("Could not open codec: %s (%d)", ::avcodec_get_name(codec_id), codec_id);
		return false;
	}

	// Generates a thread that reads and encodes frames in the input_buffer queue and places them in the output queue.
	try
	{
		_kill_flag = false;

		_codec_thread = std::thread(&TranscodeEncoder::CodecThread, this);
		pthread_setname_np(_codec_thread.native_handle(), ov::String::FormatString("Enc%s", avcodec_get_name(GetCodecID())).CStr());
	}
	catch (const std::system_error &e)
	{
		logte("Failed to start encoder thread.");
		_kill_flag = true;

		return false;
	}

	return true;
}

void EncoderAVCxOpenH264::CodecThread()
{
	while (!_kill_flag)
	{
		auto obj = _input_buffer.Dequeue();
		if (obj.has_value() == false)
			continue;

		auto media_frame = std::move(obj.value());

		//mix with commentor
		if(!_mixerFrames.empty())
	    {
	        auto mixerFrame=_mixerFrames.front();
		    _mixerFrames.pop();

		    MixFrames(media_frame, std::move(mixerFrame)); 

		   //logti("OME-MIXER: TranscodeEncoder::SendBuffer, mixer queue size: %d", _mixerFrames.size());
	    }


		///////////////////////////////////////////////////
		// Request frame encoding to codec
		///////////////////////////////////////////////////
		auto av_frame = ffmpeg::Conv::ToAVFrame(cmn::MediaType::Video, media_frame);
		if (!av_frame)
		{
			logte("Could not allocate the video frame data");
			break;
		}

		// AV_Frame.pict_type must be set to AV_PICTURE_TYPE_NONE. This will ensure that the keyframe interval option is applied correctly.
		av_frame->pict_type = AV_PICTURE_TYPE_NONE;

		int ret = ::avcodec_send_frame(_codec_context, av_frame);
		if (ret < 0)
		{
			logte("Error sending a frame for encoding : %d", ret);
		}

		///////////////////////////////////////////////////
		// The encoded packet is taken from the codec.
		///////////////////////////////////////////////////
		while (true)
		{
			// Check frame is available
			int ret = ::avcodec_receive_packet(_codec_context, _packet);
			if (ret == AVERROR(EAGAIN))
			{
				// More packets are needed for encoding.
				break;
			}
			else if (ret == AVERROR_EOF && ret < 0)
			{
				logte("Error receiving a packet for decoding : %d", ret);
				break;
			}
			else
			{
				auto media_packet = ffmpeg::Conv::ToMediaPacket(_packet, cmn::MediaType::Video, cmn::BitstreamFormat::H264_ANNEXB, cmn::PacketType::NALU);
				if (media_packet == nullptr)
				{
					logte("Could not allocate the media packet");
					break;
				}

				::av_packet_unref(_packet);

				SendOutputBuffer(std::move(media_packet));
			}
		}
	}
}

bool EncoderAVCxOpenH264::ScaleFrame(std::shared_ptr<const MediaFrame> srcFrame, AVFrame* dstFrame){
  
  auto ret=sws_scale_frame(_scaleContext,dstFrame,srcFrame->GetPrivData());
  if(ret<=0){
    
	 logti("OME-DEBUG: ERROR: scaleFrame: cannot scale frame: %d", ret);
     return false;
  }  

  return true;
}

bool EncoderAVCxOpenH264::CreateContext(AVPixelFormat format, const uint32_t& srcWidth, const uint32_t& srcHeight,
                   const uint32_t& targetWidth, const uint32_t& targetHeight)
{

  AVPixelFormat sourceFormat=format;
  AVPixelFormat targetFormat=format;
  
  if (_scaleContext!=nullptr) sws_freeContext(_scaleContext);	
   
   _scaleContext=sws_getContext(srcWidth, srcHeight,sourceFormat, 
                                targetWidth, targetHeight,targetFormat,
                                SWS_BICUBIC, 0, 0, 0);
 
   if(_scaleContext==nullptr){
      
	  logti("OME-MIXER:: Error while creating a scale context");
      return false;
   }

   return true;
}

AVFrame* EncoderAVCxOpenH264::CreateAvFrame(AVPixelFormat format,
                     const uint32_t& width, const uint32_t& height)
{

	AVFrame *frame=av_frame_alloc();

	frame->format = format;
    frame->width  = width;
    frame->height = height;

	av_frame_get_buffer(frame, 32);

	logti("OME-MIXER:: AV Frame has been created successfully");
	
    return frame;
}

void EncoderAVCxOpenH264::CopyFrameData(const AVFrame* dstFrame, const AVFrame* srcFrame,
	                    const uint32_t& width, const uint32_t& height, const uint8_t& index)
{
    for (uint32_t y = 0; y < height; y++)
	   for (uint32_t x = 0; x < width; x++)
			dstFrame->data[index][y * dstFrame->linesize[index] + x] = 
				       srcFrame->data[index][y * srcFrame->linesize[index] + x];

			
}
void EncoderAVCxOpenH264::MixFrames(const std::shared_ptr<const MediaFrame>& dstFrame,
	                                    const std::shared_ptr<const MediaFrame>& mixerFrame)
{
   auto format=mixerFrame->GetFormat();

   if(_scaleContext==nullptr)
   {
	    double aratio = (double) mixerFrame->GetWidth() / mixerFrame->GetHeight();
        mixerFrameWidth = aratio * mixerFrameHeight;
   
		CreateContext((AVPixelFormat)format, mixerFrame->GetWidth(),mixerFrame->GetHeight(),mixerFrameWidth,mixerFrameHeight);
   }

   if(_avMixerFrame==nullptr)
   {
      _avMixerFrame=CreateAvFrame((AVPixelFormat)format, mixerFrameWidth,mixerFrameHeight);
   }
   

   if(!_scaleContext || !_avMixerFrame)
   {
      return;
   }

   if(!ScaleFrame(mixerFrame, _avMixerFrame))
   {
	  return;
   }

   AVFrame* dstFrameData=dstFrame->GetPrivData();

   //copy scaled frame data into source one
   CopyFrameData(dstFrameData, _avMixerFrame, mixerFrameWidth, mixerFrameHeight,0);
   CopyFrameData(dstFrameData, _avMixerFrame, (uint32_t)(mixerFrameWidth/2.0), (uint32_t)(mixerFrameHeight/2.0),1);
   CopyFrameData(dstFrameData, _avMixerFrame, (uint32_t)(mixerFrameWidth/2.0), (uint32_t)(mixerFrameHeight/2.0),2);
}

void EncoderAVCxOpenH264::OnMixerAppFrame(const std::shared_ptr<const MediaFrame>& frame)
 {
	constexpr size_t MAX_BUFFER_SIZE=500;

	//just to be safe!
    if(_mixerFrames.size()<MAX_BUFFER_SIZE)
	{
        _mixerFrames.emplace(frame);
	}
 }

