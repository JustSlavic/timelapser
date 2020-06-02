#include <frame.h>
#include <logging.h>
#include <cstring>


namespace my {

Frame::Frame(void *data_, size_t size_) {
    data = malloc(size_);
    size = size_;

    memcpy(data, data_, size_);

    LOG_DEBUG << "Frame default constructed.";
}


Frame::Frame(const Frame &other) {
    data = malloc(other.size);
    size = other.size;

    memcpy(data, other.data, other.size);

    LOG_DEBUG << "Frame copy constructed.";
}


Frame::Frame(Frame &&other) {
    data = other.data;
    size = other.size;

    other.data = nullptr;
    other.size = 0;

    LOG_DEBUG << "Frame move constructed.";
}


Frame::~Frame() {
    if (data) free(data);
    LOG_DEBUG << "Frame destructed.";
}

}
