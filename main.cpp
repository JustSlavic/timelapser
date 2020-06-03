#include <iostream>
#include <cstdio>
#include <vector>
#include <thread>

#include <webcamera.h>
#include <video_renderer.h>
#include <logging.h>


int main(int argc, char **argv) {
    try {
        LogGlobalContext::instance()
            .set_level(Log::Level::Debug)
            .attach(std::cout);

        my::WebCamera camera;
        camera.open("/dev/video0");
        camera.init_buffers(2);
        camera.start();

        std::vector<my::Frame> frames;
        frames.reserve(5000);

        my::VideoRenderer renderer;
        renderer.find_codec("H264");

        for (int i = 0; i < 300; ++i) {
            auto frame = camera.get_frame();
            frames.push_back(std::move(frame));
            // std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        renderer.render(frames);
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        exit(EXIT_FAILURE);
    }

    return 0;
}
