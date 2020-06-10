#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>


struct Camera {
    int fd;

    size_t n_buffers;
    struct {
        uint8_t *data;
        size_t b_size;
    } *buffers;
};

struct Frame {
    uint8_t *data;
    size_t size;
};

void open_camera(struct Camera *camera, const char *device) {
    {
        camera->fd = open(device, O_RDWR);
        if (camera->fd < 0) {
            fprintf(stderr, "Web camera %s not found\n", device);
            exit(EXIT_FAILURE);
        }
    }

    {
        struct v4l2_capability capability;
        memset(&capability, 0, sizeof(struct v4l2_capability));

        if (ioctl(camera->fd, VIDIOC_QUERYCAP, &capability) < 0) {
            fprintf(stderr, "Failed to get device capabilities, VIDIOC_QUERYCAP\n");
            exit(EXIT_FAILURE);
        }
    }

    {
        struct v4l2_format image_format;
        memset(&image_format, 0, sizeof(struct v4l2_format));
        image_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(camera->fd, VIDIOC_G_FMT, &image_format) < 0) {
            fprintf(stderr, "Device could not get image format\n");
            exit(EXIT_FAILURE);
        }
    }
}

void init_buffers(struct Camera *camera, size_t n) {
    struct v4l2_requestbuffers request;
    memset(&request, 0, sizeof(struct v4l2_requestbuffers));
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    request.count = n;

    if (ioctl(camera->fd, VIDIOC_REQBUFS, &request) < 0) {
        fprintf(stderr, "Could not request buffer from device, VIDIOC_REQBUFS\n");
        exit(EXIT_FAILURE);
    }

    if (request.count < 1) {
        fprintf(stderr, "Insufficient buffer memory\n");
        exit(EXIT_FAILURE);
    }

    camera->buffers = calloc(request.count, sizeof(*camera->buffers));
    camera->n_buffers = request.count;

    for (unsigned i = 0; i < request.count; ++i) {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(struct v4l2_buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (ioctl(camera->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            fprintf(stderr, "Could not query this buffer\n");
            exit(EXIT_FAILURE);
        }

        void *memory = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE,
            MAP_SHARED, camera->fd, buffer.m.offset);

        if (memory == MAP_FAILED) {
            fprintf(stderr, "Could not mmap memory for buffer\n");
            exit(EXIT_FAILURE);
        }

        camera->buffers[i].data = memory;
        camera->buffers[i].b_size = buffer.length;
    }
}

void start_camera(struct Camera *camera) {
    for (size_t i = 0; i < camera->n_buffers; ++i) {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(struct v4l2_buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (ioctl(camera->fd, VIDIOC_QBUF, &buffer) < 0) {
            fprintf(stderr, "Cannot queue buffer\n");
            exit(EXIT_FAILURE);
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera->fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "Cannot start video stream from camera\n");
        exit(EXIT_FAILURE);
    }
}

void get_frame(struct Camera *camera, struct Frame *frame) {
    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(struct v4l2_buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (ioctl(camera->fd, VIDIOC_DQBUF, &buffer) < 0) {
        fprintf(stderr, "Failed dequeue buffer\n");
        exit(EXIT_FAILURE);
    }

    frame->data = malloc(buffer.bytesused);
    frame->size = buffer.bytesused;
    memcpy(frame->data, camera->buffers[buffer.index].data, buffer.bytesused);

    if (ioctl(camera->fd, VIDIOC_QBUF, &buffer) < 0) {
        fprintf(stderr, "Cannot queue buffer\n");
        exit(EXIT_FAILURE);
    }
}

void stop_camera(struct Camera *camera) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera->fd, VIDIOC_STREAMOFF, &type) < 0) {
        fprintf(stderr, "Cannot stop video stream from camera\n");
        exit(EXIT_FAILURE);
    }
}

void close_camera(struct Camera *camera) {
    for (int i = 0; i < camera->n_buffers; ++i) {
        munmap(camera->buffers[i].data, camera->buffers[i].b_size);
    }
    close(camera->fd);
}

void encode(AVCodecContext *codec_contex, AVFrame *frame, AVPacket *packet, AVFormatContext *);

int main() {
    const char *output_filename = "mwe_video.mp4";
    av_log_set_level(AV_LOG_DEBUG);

    struct Camera camera;
    open_camera(&camera, "/dev/video0");
    init_buffers(&camera, 2);
    start_camera(&camera);

    size_t n_frames = 600;
    struct Frame *frames = calloc(n_frames, sizeof(struct Frame));

    for (int i = 0; i < n_frames; ++i) {
        get_frame(&camera, frames + i);
        printf("Shot %d-th frame (%5.1f%%)\n", i, (i+1)*100.0/n_frames);
    }

    {
        AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);

        if (codec == NULL) {
            fprintf(stderr, "Could not find codec by name H264\n");
            exit(EXIT_FAILURE);
        }

        printf("Codec H264 found");

        AVCodecContext *codec_context = avcodec_alloc_context3(codec);
        if (codec_context == NULL) {
            fprintf(stderr, "Could not allocate codec context for codec H264\n");
            exit(EXIT_FAILURE);
        }

        /* set codec parameters */
        codec_context->bit_rate = 400000;
        codec_context->width = 640;
        codec_context->height = 480;
        codec_context->time_base = (AVRational){1, 30};
        codec_context->framerate = (AVRational){30, 1};

        /* emit one intra frame every ten frames
         * check frame pict_type before passing frame
         * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
         * then gop_size is ignored and the output of encoder
         * will always be I frame irrespective to gop_size
         */
        codec_context->gop_size = 10;     // magic
        codec_context->max_b_frames = 1;  // magic
        // codec_context->pix_fmt = AV_PIX_FMT_YUYV422;  // Not supported?
        codec_context->pix_fmt = AV_PIX_FMT_YUV422P;
        // codec_context->pix_fmt = AV_PIX_FMT_YUV420P;

        if (codec->id == AV_CODEC_ID_H264) {
            av_opt_set(codec_context->priv_data, "preset", "slow", 0); // magic
        }

        if (avcodec_open2(codec_context, codec, NULL) < 0) {
            fprintf(stderr, "Could not open codec\n");
            exit(EXIT_FAILURE);
        }

        printf("Codec context allocated\n");

        /* Prepare output file */
        struct AVFormatContext *output_format_context = NULL;
        if (avformat_alloc_output_context2(&output_format_context, NULL, "mp4", output_filename) < 0) {
            fprintf(stderr, "Could not allocate output format context\n");
            exit(EXIT_FAILURE);
        }

        AVStream *out_video_stream = avformat_new_stream(output_format_context, NULL);
        if (out_video_stream == NULL) {
            fprintf(stderr, "Could not create video stream in output format\n");
            exit(EXIT_FAILURE);
        }

        if (avcodec_parameters_from_context(out_video_stream->codecpar, codec_context) < 0) {
            fprintf(stderr, "Could not associate codec parameters with format\n");
            exit(EXIT_FAILURE);
        }

        /* Create output file */
        if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&output_format_context->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
                fprintf(stderr, "Could not open output file\n");
                exit(EXIT_FAILURE);
            }
        }

        /* Write file header */
        if (avformat_write_header(output_format_context, NULL) < 0) {
            fprintf(stderr, "Could not write format header\n");
            exit(EXIT_FAILURE);
        }

        AVFrame *av_frame = av_frame_alloc();
        if (av_frame == NULL) {
            fprintf(stderr, "Could not allocate av_frame\n");
            exit(EXIT_FAILURE);
        }

        av_frame->format = codec_context->pix_fmt;
        av_frame->width  = codec_context->width;
        av_frame->height = codec_context->height;

        AVPacket *av_packet = av_packet_alloc();
        if (av_packet == NULL) {
            fprintf(stderr, "Could not allocate av_packet\n");
            exit(EXIT_FAILURE);
        }

        // magic align=32
        if (av_frame_get_buffer(av_frame, 32) < 0) {
            fprintf(stderr, "Could not allocate the video av_frame buffer\n");
            exit(EXIT_FAILURE);
        }

        printf("File output.mp4 open\n"
               "Start rendering\n"
               "Frame:\n"
               "    size:     %dx%d\n"
               "    linesize: [%d, %d, %d]\n",
               av_frame->width, av_frame->height,
               av_frame->linesize[0], av_frame->linesize[1], av_frame->linesize[2]);

        for (int i = 0; i < n_frames; ++i) {
            if (av_frame_make_writable(av_frame) < 0) {
                fprintf(stderr, "Could not make av_frame writable\n");
                exit(EXIT_FAILURE);
            }

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

            encode(codec_context, av_frame, av_packet, output_format_context);

            if ((i+1) % 10 == 0) {
                printf("Progress %5.2f%%\n", (i+1) * 100.0 / n_frames);
            }
        }

        encode(codec_context, NULL, av_packet, output_format_context);

        if (av_write_trailer(output_format_context) < 0) {
            fprintf(stderr, "Could not write format trailer\n");
            exit(EXIT_FAILURE);
        }

        if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_format_context->pb);
        }

        printf("File %s saved\n", output_filename);

        avformat_free_context(output_format_context);
        av_frame_free(&av_frame);
        av_packet_free(&av_packet);
    }

    /* freeing frames */
    for (int i = 0; i < n_frames; ++i) {
        if (frames[i].data) free(frames[i].data);
    }
    free(frames);

    stop_camera(&camera);
    close_camera(&camera);

    return 0;
}

void encode(AVCodecContext *codec_context, AVFrame *frame, AVPacket *packet, AVFormatContext *output_format_context) {
    int ret;

    /* send the frame to the encoder */
    if (frame) printf("Send frame %3"PRId64"\n", frame->pts);

    ret = avcodec_send_frame(codec_context, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        printf("Write packet %3"PRId64" (size=%5d)\n", packet->pts, packet->size);

        if (av_interleaved_write_frame(output_format_context, packet) < 0) {
            fprintf(stderr, "Could not write av_packet\n");
            exit(EXIT_FAILURE);
        }

        av_packet_unref(packet);
    }
}
