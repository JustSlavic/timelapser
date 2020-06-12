#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>



#define PROGRESS(MSG, K, N) static size_t i_progress__ = 1;\
    if (i_progress__ == N) { printf("\r"MSG" 100.0%%\n"); }\
    else if (i_progress__ % K == 0) { printf("\r"MSG" %5.1lf%%", i_progress__ * 100.0 / N); fflush(stdout); }\
    i_progress__++


enum IOMethod {
    IO_METHOD_READ,
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR,
};

static enum IOMethod io = IO_METHOD_USERPTR;


enum ErrorType {
    ERROR_CAMERA_DEVICE = 100,
    ERROR_CAMERA_SETTINGS = 101,
};

long long get_useconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return tv.tv_usec + tv.tv_sec*1000000LL;
}


struct Camera {
    int fd;

    size_t n_buffers;
    struct {
        uint8_t *data;
        size_t b_size;
    } *buffers;

    struct {
        int numerator;
        int denominator;
    } time_base;

    struct {
        int width;
        int height;
    } resolution;

    size_t image_size;
};

struct Frame {
    uint8_t *data;
    size_t size;
};

void open_camera(struct Camera *camera, const char *device) {
    struct stat st;

    if (stat(device, &st) < 0) {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n", device, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (!S_ISCHR(st.st_mode)) {
        fprintf(stderr, "%s is no device\n", device);
        exit(EXIT_FAILURE);
    }

    camera->fd = open(device, O_RDWR);
    if (!camera->fd) {
        fprintf(stderr, "Could not open device %s\n", device);
        exit(ERROR_CAMERA_DEVICE);
    }

    struct v4l2_capability capability;
    memset(&capability, 0, sizeof(struct v4l2_capability));

    if (ioctl(camera->fd, VIDIOC_QUERYCAP, &capability) < 0) {
        fprintf(stderr, "Could not query camera capabilities (VIDIOC_QUERYCAP)\n");
        exit(ERROR_CAMERA_SETTINGS);
    }

    printf("Camera capabilities:\n"
           "  driver: %s v%u.%u.%u\n"
           "  device: %s\n"
           "  bus info: %s\n",
           capability.driver,
           (capability.version >> 16) & 0xFF,
           (capability.version >> 8) & 0xFF,
           (capability.version    ) & 0xFF,
           capability.card,
           capability.bus_info
           );

    struct v4l2_format image_format;
    memset(&image_format, 0, sizeof(struct v4l2_format));
    image_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(camera->fd, VIDIOC_G_FMT, &image_format) < 0) {
        fprintf(stderr, "Could not get image format\n");
        exit(EXIT_FAILURE);
    }

    // image_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    // image_format.fmt.pix.width = 640; // 960; // 1280;
    // image_format.fmt.pix.height = 480; // 544; // 720;

    // if (ioctl(camera->fd, VIDIOC_S_FMT, &image_format) < 0) {
    //     fprintf(stderr, "Could not set image format\n");
    // }

    camera->resolution.width = image_format.fmt.pix.width;
    camera->resolution.height = image_format.fmt.pix.height;
    camera->image_size = image_format.fmt.pix.sizeimage;
    printf("Chosen resolution %dx%d (%ld bytes)\n",
        camera->resolution.width, camera->resolution.height, camera->image_size);

    struct v4l2_streamparm parm;
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(camera->fd, VIDIOC_G_PARM, &parm) < 0) {
        fprintf(stderr, "Cannot get stream paramters\n");
        exit(EXIT_FAILURE);
    }

    // parm.parm.capture.timeperframe.numerator = 1;
    // parm.parm.capture.timeperframe.denominator = 30;

    // if (ioctl(camera->fd, VIDIOC_S_PARM, &parm) < 0) {
    //     fprintf(stderr, "Cannot set stream paramters\n");
    //     exit(EXIT_FAILURE);
    // }

    printf("Camera time per frame = %d/%d\n",
        parm.parm.capture.timeperframe.numerator,
        parm.parm.capture.timeperframe.denominator);

    camera->time_base.numerator = parm.parm.capture.timeperframe.numerator;
    camera->time_base.denominator = parm.parm.capture.timeperframe.denominator;
}

void init_buffers(struct Camera *camera, size_t n) {
    if (io == IO_METHOD_MMAP) {
        printf("Initig %ld buffers usig IO_METHOD_MMAP\n", n);

        struct v4l2_requestbuffers request;
        memset(&request, 0, sizeof(struct v4l2_requestbuffers));
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        request.count = n;

        ioctl(camera->fd, VIDIOC_REQBUFS, &request);

        camera->buffers = calloc(request.count, sizeof(*camera->buffers));
        camera->n_buffers = request.count;

        for (unsigned i = 0; i < request.count; ++i) {
            struct v4l2_buffer buffer;
            memset(&buffer, 0, sizeof(struct v4l2_buffer));
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = i;

            ioctl(camera->fd, VIDIOC_QUERYBUF, &buffer);

            void *memory = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE,
                MAP_SHARED, camera->fd, buffer.m.offset);

            camera->buffers[i].data = memory;
            camera->buffers[i].b_size = buffer.length;
        }
    }
    else if (io == IO_METHOD_USERPTR) {
        printf("Initig %ld buffers usig IO_METHOD_USERPTR\n", n);

        struct v4l2_requestbuffers request;
        memset(&request, 0, sizeof(struct v4l2_requestbuffers));
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_USERPTR;
        request.count = n;

        if (ioctl(camera->fd, VIDIOC_REQBUFS, &request) < 0) {
            if (errno == EINVAL) {
                fprintf(stderr, "Device does not support user pointer io method.\n");
            } else {
                fprintf(stderr, "Error in requesting buffers.\n");
            }
            exit(EXIT_FAILURE);
        }

        camera->buffers = calloc(request.count, sizeof(*camera->buffers));
        camera->n_buffers = request.count;

        if (!camera) {
            fprintf(stderr, "Out of memory!\n");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < camera->n_buffers; ++i) {
            camera->buffers[i].b_size = camera->image_size;
            camera->buffers[i].data = malloc(camera->image_size);

            if (!camera->buffers[i].data) {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    else {
        fprintf(stderr, "Unsupported io method. Exiting.\n");
        exit(EXIT_FAILURE);
    }
}

void start_camera(struct Camera *camera) {
    if (io == IO_METHOD_MMAP) {
        for (size_t i = 0; i < camera->n_buffers; ++i) {
            struct v4l2_buffer buffer;
            memset(&buffer, 0, sizeof(struct v4l2_buffer));

            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = i;

            if (ioctl(camera->fd, VIDIOC_QBUF, &buffer) < 0) {
                fprintf(stderr, "Could not queue buffer\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    else if (io == IO_METHOD_USERPTR) {
        for (int i = 0; i < camera->n_buffers; ++i) {
            struct v4l2_buffer buffer;
            memset(&buffer, 0, sizeof(struct v4l2_buffer));

            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_USERPTR;
            buffer.index = i;

            buffer.m.userptr = (unsigned long) camera->buffers[i].data;
            buffer.length = camera->buffers[i].b_size;

            if (ioctl(camera->fd, VIDIOC_QBUF, &buffer) < 0) {
                fprintf(stderr, "Could not queue buffer\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    else {
        fprintf(stderr, "Unsupported io method. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(camera->fd, VIDIOC_STREAMON, &type);
}

void get_frame(struct Camera *camera, struct Frame *frame) {
    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(struct v4l2_buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (io == IO_METHOD_MMAP) {
        buffer.memory = V4L2_MEMORY_MMAP;

        long long t0 = get_useconds();

        ioctl(camera->fd, VIDIOC_DQBUF, &buffer);

        long long t1 = get_useconds();
        printf("%llds and %lldus.\n", (t1 - t0) / 1000000, (t1 - t0) % 1000000);
        printf("buffer.index = %d\n", buffer.index);

        frame->data = malloc(buffer.bytesused);
        frame->size = buffer.bytesused;
        memcpy(frame->data, camera->buffers[buffer.index].data, buffer.bytesused);

        ioctl(camera->fd, VIDIOC_QBUF, &buffer);
    }
    else if (io == IO_METHOD_USERPTR) {
        buffer.memory = V4L2_MEMORY_USERPTR;

        long long t0 = get_useconds();

        if (ioctl(camera->fd, VIDIOC_DQBUF, &buffer) < 0) {
            fprintf(stderr, "Could not dequeue buffer\n");
            exit(EXIT_FAILURE);
        }

        long long t1 = get_useconds();
        printf("\r%llds and %lldus.; ", (t1 - t0) / 1000000, (t1 - t0) % 1000000);
        printf("buffer.index = %d; ", buffer.index);
        fflush(stdout);

        int i;
        for (i = 0; i < camera->n_buffers; ++i) {
            if (buffer.m.userptr == (unsigned long)camera->buffers[i].data
                && buffer.length == camera->buffers[i].b_size) {
                break;
            }
        }

        assert(i < camera->n_buffers);

        frame->data = malloc(buffer.bytesused);
        frame->size = buffer.bytesused;
        memcpy(frame->data, camera->buffers[buffer.index].data, buffer.bytesused);

        if (ioctl(camera->fd, VIDIOC_QBUF, &buffer) < 0) {
            fprintf(stderr, "Could not queue buffer\n");
            exit(EXIT_FAILURE);
        }
    }
    else {
        fprintf(stderr, "Unsupported io method\n");
        exit(EXIT_FAILURE);
    }
}

void stop_camera(struct Camera *camera) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(camera->fd, VIDIOC_STREAMOFF, &type);
}

void close_camera(struct Camera *camera) {
    if (io == IO_METHOD_MMAP) {
        for (int i = 0; i < camera->n_buffers; ++i) {
            munmap(camera->buffers[i].data, camera->buffers[i].b_size);
        }
    }
    else if (io == IO_METHOD_USERPTR) {
        for (int i = 0; i < camera->n_buffers; ++i) {
            free(camera->buffers[i].data);
        }
    }

    free(camera->buffers);
    close(camera->fd);
}

void encode(AVCodecContext *codec_contex, AVFrame *frame, AVPacket *packet, AVStream*, AVFormatContext *);
void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt);

int main(int argc, char **argv) {
    double duration = 0;

    if (argc < 2) {
        duration = 10;
        fprintf(stderr, "Duration argument not set, duration = 10s.\n");
    } else {
        duration = atof(argv[1]);
    }

    const char *output_filename = "mwe_video.mp4";
    av_log_set_level(AV_LOG_WARNING);

    struct Camera camera;
    open_camera(&camera, "/dev/video0");
    init_buffers(&camera, 5);
    start_camera(&camera);

    size_t n_frames = duration * camera.time_base.denominator / camera.time_base.numerator;
    fprintf(stderr, "n_frames = %ld\n", n_frames);
    struct Frame *frames = calloc(n_frames, sizeof(struct Frame));

    for (int i = 0; i < n_frames; ++i) {
        get_frame(&camera, frames + i);

        // PROGRESS("Filming", 5, n_frames);
        // if ((i+1) % 10 == 0) { printf("Filming progress: %5.2f%%\n", (i+1)*100.0/n_frames); }
    }

    exit(0);

    stop_camera(&camera);
    close_camera(&camera);


    {
        AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        AVCodecContext *codec_context = avcodec_alloc_context3(codec);

        /* set codec parameters */
        codec_context->bit_rate = 400000;
        codec_context->width = camera.resolution.width;
        codec_context->height = camera.resolution.height;
        codec_context->time_base = (AVRational){camera.time_base.numerator, camera.time_base.denominator};
        codec_context->framerate = (AVRational){camera.time_base.denominator, camera.time_base.numerator};

        /* emit one intra frame every ten frames
         * check frame pict_type before passing frame
         * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
         * then gop_size is ignored and the output of encoder
         * will always be I frame irrespective to gop_size
         */
        codec_context->gop_size = 10;
        codec_context->max_b_frames = 1;
        codec_context->pix_fmt = AV_PIX_FMT_YUV422P;

        if (codec->id == AV_CODEC_ID_H264) {
            av_opt_set(codec_context->priv_data, "preset", "slow", 0);
        }

        avcodec_open2(codec_context, codec, NULL);

        struct AVFormatContext *output_format_context = NULL;
        avformat_alloc_output_context2(&output_format_context, NULL, "mp4", output_filename);

        AVStream *out_video_stream = avformat_new_stream(output_format_context, NULL);

        avcodec_parameters_from_context(out_video_stream->codecpar, codec_context);
        out_video_stream->time_base = codec_context->time_base;
        // out_video_stream->avg_frame_rate = codec_context->framerate;

        if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_open(&output_format_context->pb, output_filename, AVIO_FLAG_WRITE);
        }

        avformat_write_header(output_format_context, NULL);

        AVFrame *av_frame = av_frame_alloc();
        av_frame->format = codec_context->pix_fmt;
        av_frame->width  = codec_context->width;
        av_frame->height = codec_context->height;

        AVPacket *av_packet = av_packet_alloc();

        av_frame_get_buffer(av_frame, 0);

        for (int i = 0; i < n_frames; ++i) {
            av_frame_make_writable(av_frame);

            /*
             *  YUYV pixel layout:
             *
             *  [Y U Y V] - two pixels in a row
             */
            int i_data = 0;
            for (int y = 0; y < codec_context->height; ++y) {
                for (int x = 0, i_cb = 0, i_cr = 0; x < codec_context->width;) {
                    // Y channel
                    av_frame->data[0][y * av_frame->linesize[0] + x++] = frames[i].data[i_data++];
                    // Cb channel
                    av_frame->data[1][y * av_frame->linesize[1] + i_cb++] = frames[i].data[i_data++];
                    // Y channel
                    av_frame->data[0][y * av_frame->linesize[0] + x++] = frames[i].data[i_data++];
                    // Cr channel
                    av_frame->data[2][y * av_frame->linesize[2] + i_cr++] = frames[i].data[i_data++];
                }
            }

            av_frame->pts = i;

            encode(codec_context, av_frame, av_packet, out_video_stream, output_format_context);
            PROGRESS("Encoding", 10, n_frames);
            // if ((i+1) % 10 == 0) { printf("Encoding progress: %5.2f%%\n", (i+1)*100.0/n_frames); }
        }

        encode(codec_context, NULL, av_packet, out_video_stream, output_format_context);

        av_write_trailer(output_format_context);

        if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_format_context->pb);
        }

        avformat_free_context(output_format_context);
        av_frame_free(&av_frame);
        av_packet_free(&av_packet);
    }

    for (int i = 0; i < n_frames; ++i) {
        if (frames[i].data) free(frames[i].data);
    }
    free(frames);

    return 0;
}

void encode(
    AVCodecContext *codec_context,
    AVFrame *frame,
    AVPacket *packet,
    AVStream *stream,
    AVFormatContext *output_format_context) {
    int ret;

    ret = avcodec_send_frame(codec_context, frame);

    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return;


        av_packet_rescale_ts(packet, codec_context->time_base, stream->time_base);
        packet->stream_index = stream->index;

        // log_packet(output_format_context, packet);
        av_interleaved_write_frame(output_format_context, packet);
        av_packet_unref(packet);
    }
}

void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt) {
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}
