#include <frame.h>
#include <logging.h>
#include <cstring>


namespace my {

Frame::Frame(uint8_t *data_, size_t size_) {
    data = (uint8_t*) malloc(size_);
    size = size_;

    memcpy(data, data_, size_);
}


Frame::Frame(const Frame &other) {
    data = (uint8_t*) malloc(other.size);
    size = other.size;

    memcpy(data, other.data, other.size);
}


Frame::Frame(Frame &&other) {
    data = other.data;
    size = other.size;

    other.data = nullptr;
    other.size = 0;
}


Frame::~Frame() {
    if (data) free(data);
}

}
