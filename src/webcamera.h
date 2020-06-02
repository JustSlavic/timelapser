#pragma once

#include <frame.h>
#include <cstddef>
#include <vector>


namespace my {

struct WebCamera {
    struct FrameBuffer {
        void *start{nullptr};
        size_t size{0};

        FrameBuffer(void *data, size_t size);
        FrameBuffer(const FrameBuffer&) = delete;
        FrameBuffer(FrameBuffer&&);
        ~FrameBuffer();
    };

    enum class State {
        StreamOFF,
        StreamON,
    };

    int descriptor = 0;
    std::vector<FrameBuffer> buffers;
    State state = State::StreamOFF;

    ~WebCamera();

    void open(const char *device);
    void init_buffers(size_t n);

    void start();
    void stop();

    Frame get_frame();
};

}
