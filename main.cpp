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


static constexpr v4l2_buf_type BUFFER_TYPE = V4L2_BUF_TYPE_VIDEO_CAPTURE;

const char *read_errno() {
    switch (errno) {
        case EAGAIN:
            return "EAGAIN: The ioctl can’t be handled because the device is in state where it can’t perform it.";
            "This could happen for example in case where device is sleeping and ioctl is performed "
            "to query statistics. It is also returned when the ioctl would need to wait for an event, "
            "but the device was opened in non-blocking mode.";

        case EBADF:
            return "EBADF: The file descriptor is not a valid.";

        case EBUSY:
            return "EBUSY: The ioctl can’t be handled because the device is busy.";
            "This is typically return while device is streaming, and an ioctl tried to change something that "
            "would affect the stream, or would require the usage of a hardware resource that "
            "was already allocated. The ioctl must not be retried without performing another "
            "action to fix the problem first (typically: stop the stream before retrying).";

        case EFAULT:
            return "EFAULT: "
                   "There was a failure while copying data from/to userspace, "
                   "probably caused by an invalid pointer reference.";

        case EINVAL:
            return "EINVAL: One or more of the ioctl parameters are invalid or out of the allowed range.";
            "This is a widely used error code. See the individual ioctl requests for specific causes.";

        case ENODEV:
            return "ENODEV: Device not found or was removed.";

        case ENOMEM:
            return "ENOMEM: There’s not enough memory to handle the desired operation.";

        case ENOTTY:
            return "ENOTTY: "
                   "The ioctl is not supported by the driver, actually meaning that the "
                   "required functionality is not available, or the file descriptor is not for a media device.";

        case ENOSPC:
            return "ENOSPC: "
                   "On USB devices, the stream ioctl’s can return this error, "
                   "meaning that this request would overcommit the usb bandwidth "
                   "reserved for periodic transfers (up to 80% of the USB bandwidth).";

        case EPERM:
            return "EPERM: Permission denied.";
            "Can be returned if the device needs write permission, or some special capabilities is needed (e. g. root)";

        case EIO:
            return "I/O error.";
            "Typically used when there are problems communicating with a hardware device. "
            "This could indicate broken or flaky hardware. It’s a ‘Something is wrong, I give up!’ type of error.";

        case ENXIO:
            return "ENXIO: No device corresponding to this device special file exists.";
        default:
            return "";
    }
}

const char *pixel_format_cstr(int fmt) {
    switch (fmt) {
        case V4L2_PIX_FMT_MJPEG: return "Motion-JPEG";
        case V4L2_PIX_FMT_JPEG: return "JFIF JPEG";
        case V4L2_PIX_FMT_MPEG: return "MPEG-1/2/4 Multiplexed";
        case V4L2_PIX_FMT_MPEG1: return "MPEG-1 ES";
        case V4L2_PIX_FMT_MPEG2: return "MPEG-2 ES";
        case V4L2_PIX_FMT_MPEG4: return "MPEG-4 part 2 ES";
        default: return "Other format";
    }
}

struct frame {
    void *data = nullptr;
    size_t size = 0;

    ~frame() { if (data) free(data); }

    frame(void * d, size_t s) : size(s) {
        data = malloc(s);
        memcpy(data, d, s);
    }

    frame(const frame& other) : size(other.size) {
        data = malloc(other.size);
        memcpy(data, other.data, other.size);
    }

    frame(frame&&) = default;
};

struct web_camera {
    struct buffer {
        void *start{nullptr};
        size_t length = 0;

        ~buffer() {
            munmap(start, length);
        }
    };

    int descriptor = 0;
    int width = 0;
    int height = 0;

    v4l2_buffer m_buffer_info{};
    buffer m_buffer{};

    web_camera(int width, int height) :width(width), height(height) {
        open_device();
        ask_frame_capture_capability();
        set_format();
        query_buffer();
    }

    /**
     * Activate streaming
     */
    void start() {
        if (ioctl(descriptor, VIDIOC_STREAMON, &m_buffer_info.type) < 0) {
            throw std::runtime_error("Could not start streaming, VIDIOC_STREAMON");
        }
    }

    void stop() {
        if (ioctl(descriptor, VIDIOC_STREAMOFF, &m_buffer_info.type) < 0) {
            throw std::runtime_error("Could not stop streaming, VIDIOC_STREAMOFF");
        }
    }

    frame get_frame() {
        // Queue the buffer
        if (ioctl(descriptor, VIDIOC_QBUF, &m_buffer_info) < 0) {
            throw std::runtime_error("Could not queue buffer, VIDIOC_QBUF");
        }

        // Dequeue the buffer
        if (ioctl(descriptor, VIDIOC_DQBUF, &m_buffer_info) < 0) {
            throw std::runtime_error("Could not dequeue the buffer, VIDIOC_DQBUF");
        }

        return frame(m_buffer.start, m_buffer_info.bytesused);
    }

    ~web_camera() {
        close(descriptor);
    }

private:
    /**
     *  1. Open the video device and get the file descriptor
     */
    void open_device() {
        descriptor = open("/dev/video0", O_RDWR);
        if (descriptor < 0) { throw std::runtime_error("Web camera /dev/video0 not found"); }
    }

    /**
     *  2. Ask the device if it can capture frames
     *
     *  All V4L2 drivers must support VIDIOC_QUERYCAP.
     *  Applications should always call this ioctl after opening the device.
     */
    void ask_frame_capture_capability() const {
        v4l2_capability capability{};

        if (ioctl(descriptor, VIDIOC_QUERYCAP, &capability) < 0) {
            throw std::runtime_error("Failed to get device capabilities, VIDIOC_QUERYCAP");
        }

        printf("Info:\n");
        printf("  Driver: %s\n", capability.driver);
        printf("  Card: %s\n", capability.card);
        printf("  Bus info: %s\n", capability.bus_info);
        printf("Capabilities:\n");
        printf("  Video capture: %s\n", (capability.capabilities & V4L2_CAP_VIDEO_CAPTURE ? "true" : "false"));
        printf("  Audio: %s\n", (capability.capabilities & V4L2_CAP_AUDIO ? "true" : "false"));
        printf("  Read/Write: %s\n", (capability.capabilities & V4L2_CAP_READWRITE ? "true" : "false"));
        printf("  Streaming: %s\n", (capability.capabilities & V4L2_CAP_STREAMING ? "true" : "false"));
    }

    /**
     *  3. Set image format
     */
    void set_format() const {
        v4l2_format image_format{};
        image_format.type = BUFFER_TYPE;
        image_format.fmt.pix.width = width;
        image_format.fmt.pix.height = height;
        image_format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        image_format.fmt.pix.field = V4L2_FIELD_NONE;

        // tell the device you are using this format
        if (ioctl(descriptor, VIDIOC_S_FMT, &image_format) < 0) {
            throw std::runtime_error("Device could not get image format");
        }

        printf("Negotiated image format:\n");
        printf("  Resolution: %dx%d\n", image_format.fmt.pix.width, image_format.fmt.pix.height);
        printf("  Pixel format: %s\n", pixel_format_cstr(image_format.fmt.pix.pixelformat));
        printf("  Image size: %d bytes\n", image_format.fmt.pix.sizeimage);
    }

    /**
     * 4. Request buffers from the device
     * 5. mmap them into app's memory
     * @param count
     */
    void query_buffer() {
        // 4. Request Buffers from the device
        v4l2_requestbuffers request_buffer = {0};
        request_buffer.type = BUFFER_TYPE;
        request_buffer.memory = V4L2_MEMORY_MMAP;
        request_buffer.count = 1;

        if (ioctl(descriptor, VIDIOC_REQBUFS, &request_buffer) < 0) {
            throw std::runtime_error("Could not request buffer from device, VIDIOC_REQBUFS");
        }

        // 5. Query the buffer to get raw data ie. ask for the you requested buffer and allocate memory for it
        m_buffer_info.type = request_buffer.type;
        m_buffer_info.memory = request_buffer.memory;
        m_buffer_info.index = 0;

        if (ioctl(descriptor, VIDIOC_QUERYBUF, &m_buffer_info) < 0) {
            throw std::runtime_error("Device did not return the buffer information, VIDIOC_QUERYBUF");
        }

        // use a pointer to point to the newly created buffer
        // mmap() will map the memory address of the device to an address in memory
        m_buffer.start = (char *) mmap(nullptr, m_buffer_info.length,
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       descriptor, m_buffer_info.m.offset);
        m_buffer.length = m_buffer_info.length;
        if (m_buffer.start == MAP_FAILED) {
            throw std::runtime_error("mmap failed");
        }

        memset(m_buffer.start, 0, m_buffer.length);
    }
};

int main() {
    try {
        web_camera device(1280, 640);
        device.start();

        std::vector<frame> frames;
        frames.reserve(300);

        for (int i = 0; i < 5; ++i) {
            frame f = device.get_frame();
            frames.push_back(f);
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        device.stop();

        for (int i = 0; i < frames.size(); ++i) {
            std::ofstream out;
            out.open("webcam" + std::to_string(i) + ".jpg", std::ios::binary);
            out.write((char*)frames[i].data, frames[i].size);
            out.close();
        }

    } catch(const std::exception& e) {
        std::cerr << e.what() << '\n' << read_errno() << std::endl;
        std::exit(errno);
    }

    return 0;
}


