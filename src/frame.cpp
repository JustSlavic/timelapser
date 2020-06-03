#include <frame.h>
#include <logging.h>
#include <cstring>


namespace my {

Frame::Frame(void *data_, size_t size_) {
    data = malloc(size_);
    size = size_;

    memcpy(data, data_, size_);
}


Frame::Frame(const Frame &other) {
    data = malloc(other.size);
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
