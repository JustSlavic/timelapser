#include <video_encoder.h>

extern "C" {
#include <libavutil/opt.h>
}

#include <cstdio>
#include <string>
#include <fstream>
#include <stdexcept>

#include <logging.h>


namespace my {

static const AVPixelFormat pixel_format = AV_PIX_FMT_YUYV422;


VideoEncoder::VideoEncoder() {}

VideoEncoder::~VideoEncoder() {
    if (codec_context) { avcodec_free_context(&codec_context); }
}


void VideoEncoder::find_codec(const char *name) {
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);

    if (codec == nullptr) {
        throw std::runtime_error("Could not find codec by name " + std::string(name));
    }

    LOG_DEBUG << "Codec H264 found";

    codec_context = avcodec_alloc_context3(codec);
    if (codec_context == nullptr) {
        throw std::runtime_error("Could not allocate codec context for codec " + std::string(name));
    }

    /* set codec parameters */
    codec_context->bit_rate = 400000;
    codec_context->width = 640;
    codec_context->height = 480;
    codec_context->time_base = (AVRational){1, 30};
    codec_context->framerate = (AVRational){30, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    codec_context->gop_size = 10;     // magic
    codec_context->max_b_frames = 1;  // magic
    // codec_context->pix_fmt = AV_PIX_FMT_YUYV422;  // Not supported?
    codec_context->pix_fmt = AV_PIX_FMT_YUV422P;
    // codec_context->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(codec_context->priv_data, "preset", "slow", 0); // magic
    }

    if (avcodec_open2(codec_context, codec, NULL) < 0) {
        throw std::runtime_error("Could not open codec");
    }

    LOG_DEBUG << "Codec context allocated";
}


void VideoEncoder::render(const std::vector<Frame> &frames) {
    const char *filename = "data/output.mp4";
    /* Prepare output file */
    AVFormatContext *output_format_context{nullptr};
    if (avformat_alloc_output_context2(&output_format_context, nullptr, "mp4", nullptr) < 0) {
        throw std::runtime_error("Could not allocate output format context");
    }

    AVStream *out_video_stream = avformat_new_stream(output_format_context, nullptr);
    if (out_video_stream == nullptr) {
        throw std::runtime_error("Could not create video stream in output format");
    }

    if (avcodec_parameters_from_context(out_video_stream->codecpar, codec_context) < 0) {
        throw std::runtime_error("Could not associate codec parameters with format");
    }
    out_video_stream->time_base = codec_context->time_base;

    /* Create output file */
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_format_context->pb, filename, AVIO_FLAG_WRITE) < 0) {
            throw std::runtime_error("Could not open output file");
        }
    }

    /* Write file header */
    if (avformat_write_header(output_format_context, nullptr) < 0) {
        throw std::runtime_error("Could not write format header");
    }

    AVFrame *frame = av_frame_alloc();
    if (frame == nullptr) {
        throw std::runtime_error("Could not allocate frame");
    }

    frame->format = codec_context->pix_fmt;
    frame->width  = codec_context->width;
    frame->height = codec_context->height;

    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        throw std::runtime_error("Could not allocate packet");
    }

    // magic align=32
    if (av_frame_get_buffer(frame, 32) < 0) {
        throw std::runtime_error("Could not allocate the video frame buffer");
    }

    LOG_DEBUG << "File " << filename << " open";
    LOG_DEBUG << "Start rendering";

    LOG_DEBUG << "Frame:";
    LOG_DEBUG << "    size:     " << frame->width << "x" << frame->height;
    LOG_DEBUG << "    linesize: [" << frame->linesize[0]
              << ", " << frame->linesize[1] << ", " << frame->linesize[2] << "]";

    int i = 0;
    for (Frame const &frame_data : frames) {
        if (av_frame_make_writable(frame) < 0) {
            throw std::runtime_error("Could not make frame writable");
        }

        /*
         *  YUYV pixel layout:
         *
         *  [Y U Y V] - two pixels in a row
         */
        int i_data = 0;
        for (int y = 0; y < codec_context->height; ++y) {
            for (int x = 0, i_cb = 0, i_cr = 0; x < codec_context->width;) {
                // Y channel
                frame->data[0][y * frame->linesize[0] + x++] = frame_data.data[i_data++];
                // Cb channel
                frame->data[1][y * frame->linesize[1] + i_cb++] = frame_data.data[i_data++];
                // Y channel
                frame->data[0][y * frame->linesize[0] + x++] = frame_data.data[i_data++];
                // Cr channel
                frame->data[2][y * frame->linesize[2] + i_cr++] = frame_data.data[i_data++];
            }
        }

        frame->pts = i++;

        int err = avcodec_send_frame(codec_context, frame);
        if (err == AVERROR(EAGAIN)) LOG_ERROR << "EAGAIN!!!";
        if (err == AVERROR_EOF)     LOG_ERROR << "EVERROR_EOF!!!";
        if (err == AVERROR(EINVAL)) LOG_ERROR << "EINVAL!!!";
        if (err < 0) {
            throw std::runtime_error("Could not send frame to the codec");
        }

        LOG_DEBUG << "Sent frame " << frame->pts;

        while (true) {
            int err = avcodec_receive_packet(codec_context, packet);
            if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) break;
            if (err < 0) {
                throw std::runtime_error("Could not receive packet");
            }

            LOG_DEBUG << "Write packet " << packet->pts << " size: " << packet->size;

            /* rescale output packet timestamp values from codec to stream timebase */
            av_packet_rescale_ts(packet, codec_context->time_base, out_video_stream->time_base);
            packet->stream_index = out_video_stream->index;

            if (av_interleaved_write_frame(output_format_context, packet) < 0) {
                throw std::runtime_error("Could not write packet");
            }

            av_packet_unref(packet);
        }

        if (i % 10 == 0) {
            LOG_DEBUG << "Progress " << i * 100.0 / frames.size() << "%";
        }
    }

    if (avcodec_send_frame(codec_context, nullptr) < 0) {
        throw std::runtime_error("Could not flush nullptr to the codec");
    }

    if (avcodec_receive_packet(codec_context, packet) < 0) {
        throw std::runtime_error("Could not receive packet");
    }

    LOG_DEBUG << "Write packet " << packet->pts << " size: " << packet->size;

    if (av_interleaved_write_frame(output_format_context, packet) < 0) {
        throw std::runtime_error("Could not write packet");
    }

    if (av_write_trailer(output_format_context) < 0) {
        throw std::runtime_error("Could not write format trailer");
    }

    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_format_context->pb);
    }

    LOG_DEBUG << "File " << filename << " saved";

    avformat_free_context(output_format_context);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

}
