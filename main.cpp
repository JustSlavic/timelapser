#include <iostream>
#include <cstdio>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

using namespace std;

struct frame {
    char *data = nullptr;
    size_t size = 0;

    ~frame() { if (data) free(data); }

    frame(char * d, size_t s) : data(d), size(s) {}
    frame(const frame&) = delete;
    frame(frame&&) = default;
};

struct web_camera {
    int descriptor;

    web_camera() {
        descriptor = open("/dev/video0", O_RDWR);
        if (descriptor < 0) { throw std::runtime_error("Web camera /dev/video0 not found"); }
    }
};

int main() {
    // 1.  Open the device
    int fd; // A file descriptor to the video device
    fd = open("/dev/video0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device, OPEN");
        return 1;
    }


    // 2. Ask the device if it can capture frames
    v4l2_capability capability{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
        // something went wrong... exit
        perror("Failed to get device capabilities, VIDIOC_QUERYCAP");
        return 1;
    }


    // 3. Set Image format
    v4l2_format imageFormat{};
    imageFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    imageFormat.fmt.pix.width = 1024;
    imageFormat.fmt.pix.height = 1024;
    imageFormat.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    imageFormat.fmt.pix.field = V4L2_FIELD_NONE;
    // tell the device you are using this format
    if (ioctl(fd, VIDIOC_S_FMT, &imageFormat) < 0) {
        perror("Device could not set format, VIDIOC_S_FMT");
        return 1;
    }


    // 4. Request Buffers from the device
    v4l2_requestbuffers requestBuffer = {0};
    requestBuffer.count = 1; // one request buffer
    requestBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // request a buffer which we an use for capturing frames
    requestBuffer.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &requestBuffer) < 0) {
        perror("Could not request buffer from device, VIDIOC_REQBUFS");
        return 1;
    }


    // 5. Query the buffer to get raw data ie. ask for the you requested buffer
    // and allocate memory for it
    v4l2_buffer queryBuffer = {0};
    queryBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    queryBuffer.memory = V4L2_MEMORY_MMAP;
    queryBuffer.index = 0;
    if (ioctl(fd, VIDIOC_QUERYBUF, &queryBuffer) < 0) {
        perror("Device did not return the buffer information, VIDIOC_QUERYBUF");
        return 1;
    }
    // use a pointer to point to the newly created buffer
    // mmap() will map the memory address of the device to
    // an address in memory
    char *buffer = (char *) mmap(nullptr, queryBuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                 fd, queryBuffer.m.offset);
    memset(buffer, 0, queryBuffer.length);


    // 6. Get a frame
    // Create a new buffer type so the device knows whichbuffer we are talking about
    v4l2_buffer buffer_info{};
    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer_info.memory = V4L2_MEMORY_MMAP;
    buffer_info.index = 0;

    // Activate streaming
    int type = buffer_info.type;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Could not start streaming, VIDIOC_STREAMON");
        return 1;
    }

    std::vector<frame> frames;
    frames.reserve(300);

    for (int i = 0; i < 300; ++i) {
        // Queue the buffer
        if (ioctl(fd, VIDIOC_QBUF, &buffer_info) < 0) {
            perror("Could not queue buffer, VIDIOC_QBUF");
            return 1;
        }

        // Dequeue the buffer
        if (ioctl(fd, VIDIOC_DQBUF, &buffer_info) < 0) {
            perror("Could not dequeue the buffer, VIDIOC_DQBUF");
            return 1;
        }
        // Frames get written after dequeuing the buffer

        char *output = (char *) malloc(buffer_info.bytesused);
        memcpy(output, buffer, buffer_info.bytesused);
        frames.emplace_back(output, buffer_info.bytesused);

        std::this_thread::sleep_for(std::chrono::microseconds(16));
    }

    // end streaming
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("Could not end streaming, VIDIOC_STREAMOFF");
        return 1;
    }

    for (int i = 0; i < 300; ++i) {
        ofstream out;
        out.open("webcam" + std::to_string(i) + ".jpg", ios::binary | ios::app);
        out.write(frames[i].data, frames[i].size);
        out.close();
    }

    close(fd);
    return 0;
}


