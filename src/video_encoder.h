#pragma once

#include <vector>
#include <frame.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}


namespace my {

struct VideoEncoder {
    AVCodecContext *codec_context{nullptr};
    AVCodec *codec{nullptr};

    VideoEncoder();
    ~VideoEncoder();

    void find_codec(const char *name);
    void render(const std::vector<Frame> &);
    void save_to(const char *filename);
};

}
