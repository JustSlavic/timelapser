#include <iostream>
#include <fstream>
#include <cstdio>
#include <vector>
#include <thread>

#include <webcamera.h>
#include <video_encoder.h>
#include <logging.h>


void save_image(const my::Frame &frame, const std::string &filename) {
    std::ofstream out;
    out.open(filename, std::ios::binary);
    out.write((char*)frame.data, frame.size);
    out.close();
}

int main(int argc, char **argv) {
    try {
        Log::GlobalContext::instance()
            .set_level(Log::Level::Debug)
            .attach(std::cout);

        my::WebCamera camera;
        camera.open("/dev/video0");
        camera.init_buffers(2);
        camera.start();

        std::vector<my::Frame> frames;
        frames.reserve(5000);

        my::VideoEncoder encoder;
        encoder.find_codec("H264");

        int n = 50;
        LOG_DEBUG << "Going to get " << n << " frames video";
        for (int i = 0; i < n; ++i) {
            auto frame = camera.get_frame();
            frames.push_back(std::move(frame));
            // std::this_thread::sleep_for(std::chrono::microseconds(100));

            // save_image(frame, "data/webcam" + std::to_string(i) + ".jpg");

            if ((i + 1) % 10 == 0) {
                LOG_DEBUG << "Progress " << (i + 1) * 100.0 / n << "%";
            }
        }

        camera.stop();

        encoder.render(frames);
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        exit(EXIT_FAILURE);
    }

    return 0;
}
