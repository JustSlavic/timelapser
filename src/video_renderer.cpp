#include <video_renderer.h>

extern "C" {
#include <libavutil/opt.h>
}

#include <cstdio>
#include <string>
#include <stdexcept>

#include <logging.h>


namespace my {

static const AVPixelFormat pixel_format = AV_PIX_FMT_YUYV422;


VideoRenderer::VideoRenderer() {}

VideoRenderer::~VideoRenderer() {
    if (codec_context) { avcodec_free_context(&codec_context); }
}


void VideoRenderer::find_codec(const char *name) {
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
    codec_context->time_base = (AVRational){1, 25};
    codec_context->framerate = (AVRational){25, 1};

    codec_context->gop_size = 10;  // magic
    codec_context->max_b_frames = 1;  // magic
    // codec_context->pix_fmt = AV_PIX_FMT_YUV422P;
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;

    // magic
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(codec_context->priv_data, "preset", "slow", 0);
    }

    if (avcodec_open2(codec_context, codec, NULL) < 0) {
        throw std::runtime_error("Could not open codec");
    }

    LOG_DEBUG << "Codec context allocated";
}

void encode(AVCodecContext *context, AVFrame *frame, AVPacket *packet, FILE *file) {
    if (avcodec_send_frame(context, frame) < 0) {
        throw std::runtime_error("Could not send frame to the codec");
    }

    while (true) {
        int err = avcodec_receive_packet(context, packet);
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) return;
        if (err < 0) {
            throw std::runtime_error("Could not receive packet");
        }

        fwrite(packet->data, 1, packet->size, file);
        av_packet_unref(packet);
    }
}

void VideoRenderer::render(const std::vector<Frame> &frames) {
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

    FILE *out_file = fopen("output.mp4", "w");
    if (out_file == nullptr) {
        throw std::runtime_error("Could not open file output.mp4");
    }

    int i = 0;
    for (Frame const &frame_data : frames) {
        if (av_frame_make_writable(frame) < 0) {
            throw std::runtime_error("Could not make frame writable");
        }

        // prepare a dummy image
        for (int y = 0; y < codec_context->height; y++) {
            for (int x = 0; x < codec_context->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        for (int y = 0; y < codec_context->height / 2; y++) {
            for (int x = 0; x < codec_context->width / 2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        frame->pts = i;

        encode(codec_context, frame, packet, out_file);
    }

    encode(codec_context, nullptr, packet, out_file);

    uint8_t endcode[] = { 0, 0, 1, 0xb7 };
    if (codec->id == AV_CODEC_ID_MPEG1VIDEO ||
        codec->id == AV_CODEC_ID_MPEG2VIDEO) {
        fwrite(endcode, 1, sizeof(endcode), out_file);
    }

    fclose(out_file);

    av_frame_free(&frame);
    av_packet_free(&packet);
}

}
