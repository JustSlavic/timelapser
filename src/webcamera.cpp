#include <webcamera.h>
#include <logging.h>

#include <stdio.h>
#include <unistd.h>

#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <stdexcept>


namespace my {

enum IOMethod {
    IO_METHOD_READ,
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR,
};

static IOMethod io = IO_METHOD_MMAP;



WebCamera::FrameBuffer::FrameBuffer(void *data, size_t size)
    : start(data)
    , size(size)
{}

WebCamera::FrameBuffer::~FrameBuffer() {
    if (munmap(start, size) < 0) {
        LOG_ERROR << "Cannot unmap memory.";
    }
}

WebCamera::FrameBuffer::FrameBuffer(FrameBuffer &&other) {
    start = other.start;
    size = other.size;

    other.start = nullptr;
    other.size = 0;
}


const char *pixel_format_cstr(int fmt) {
    switch (fmt) {
        case V4L2_PIX_FMT_MJPEG: return "Motion-JPEG";
        case V4L2_PIX_FMT_JPEG:  return "JFIF JPEG";
        case V4L2_PIX_FMT_MPEG:  return "MPEG-1/2/4 Multiplexed";
        case V4L2_PIX_FMT_MPEG1: return "MPEG-1 ES";
        case V4L2_PIX_FMT_MPEG2: return "MPEG-2 ES";
        case V4L2_PIX_FMT_MPEG4: return "MPEG-4 part 2 ES";
        case V4L2_PIX_FMT_YUYV:  return "(YUYV) YUV 4:2:2";
        case V4L2_PIX_FMT_YYUV:  return "(YYUV) YUV 4:2:2";
        case V4L2_PIX_FMT_YVYU:  return "(YVYU) YVU 4:2:2";
        case V4L2_PIX_FMT_UYVY:  return "(UYVY) YUV 4:2:2";
        case V4L2_PIX_FMT_VYUY:  return "(VYUY) YUV 4:2:2";
        default: return "Other format";
    }
}

WebCamera::~WebCamera() {
    if (state == State::StreamON) stop();
    if (descriptor) { close(descriptor); }
}

void WebCamera::open(const char *device) {

    {
        descriptor = ::open(device, O_RDWR);
        if (descriptor < 0) {
            throw std::runtime_error("Web camera /dev/video0 not found");
        }

        LOG_INFO << "Device " << device << " open.";
    }

    {
        v4l2_capability capability{};

        if (ioctl(descriptor, VIDIOC_QUERYCAP, &capability) < 0) {
            throw std::runtime_error("Failed to get device capabilities, VIDIOC_QUERYCAP");
        }

        LOG_DEBUG << "Capabilities negotiated.";
    }

    {
        v4l2_format image_format{};
        image_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        // Request used format from the device
        if (ioctl(descriptor, VIDIOC_G_FMT, &image_format) < 0) {
            throw std::runtime_error("Device could not get image format");
        }

        LOG_DEBUG << "Negotiated image format:";
        LOG_DEBUG << "  Resolution: " << image_format.fmt.pix.width << "x" << image_format.fmt.pix.height;
        LOG_DEBUG << "  Pixel format: " << pixel_format_cstr(image_format.fmt.pix.pixelformat);
        LOG_DEBUG << "  Image size: " << image_format.fmt.pix.sizeimage << " bytes";
    }
}

void init_mmap(WebCamera *camera, size_t n) {
    v4l2_requestbuffers request;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    request.count = n;

    if (ioctl(camera->descriptor, VIDIOC_REQBUFS, &request) < 0) {
        throw std::runtime_error("Could not request buffer from device, VIDIOC_REQBUFS.");
    }

    if (request.count < 2) {
        throw std::runtime_error("Insufficient buffer memory.");
    }

    camera->buffers.reserve(request.count);

    for (int i = 0; i < request.count; ++i) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (ioctl(camera->descriptor, VIDIOC_QUERYBUF, &buffer) < 0) {
            throw std::runtime_error("Could not query this buffer.");
        }


        void *memory = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
            MAP_SHARED, camera->descriptor, buffer.m.offset);

        if (memory == MAP_FAILED) {
            throw std::runtime_error("Could not mmap memory for buffer.");
        }

        camera->buffers.emplace_back(memory, buffer.length);
    }
}

void WebCamera::init_buffers(size_t n) {
    if (io == IO_METHOD_MMAP) {
        init_mmap(this, n);
    } else {
        throw std::runtime_error("No IOMethod provided.");
    }
}

void WebCamera::start() {
    if (io == IO_METHOD_MMAP) {

        for (int i = 0; i < buffers.size(); ++i) {
            v4l2_buffer buffer;
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = i;

            if (ioctl(descriptor, VIDIOC_QBUF, &buffer) < 0) {
                throw std::runtime_error("Cannot queue buffer.");
            }
        }

        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(descriptor, VIDIOC_STREAMON, &type) < 0) {
            throw std::runtime_error("Cannot start video stream from camera.");
        }
    }

    state = State::StreamON;
    LOG_INFO << "Camera video stream started.";
}

void WebCamera::stop() {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(descriptor, VIDIOC_STREAMOFF, &type) < 0) {
        throw std::runtime_error("Cannot stop video stream from camera.");
    }

    state = State::StreamOFF;
    LOG_INFO << "Camera video stream stopped.";
}

Frame WebCamera::get_frame() {
    v4l2_buffer buffer;

    if (io == IO_METHOD_MMAP) {
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;

        if (ioctl(descriptor, VIDIOC_DQBUF, &buffer) < 0) {
            throw std::runtime_error("Failed dequeue buffer.");
        }

        Frame frame(buffers[buffer.index].start, buffer.bytesused);

        if (ioctl(descriptor, VIDIOC_QBUF, &buffer) < 0) {
            throw std::runtime_error("Cannot queue buffer.");
        }

        return frame;
    }

    throw std::runtime_error("Unsupported io method.");
}

}
