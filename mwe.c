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
#include <libavutil/timestamp.h>


#define FRAME_RATE 25

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
    camera->fd = open(device, O_RDWR);

    struct v4l2_capability capability;
    memset(&capability, 0, sizeof(struct v4l2_capability));

    ioctl(camera->fd, VIDIOC_QUERYCAP, &capability);

    struct v4l2_format image_format;
    memset(&image_format, 0, sizeof(struct v4l2_format));
    image_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ioctl(camera->fd, VIDIOC_G_FMT, &image_format);

    image_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    ioctl(camera->fd, VIDIOC_S_FMT, &image_format);
}

void init_buffers(struct Camera *camera, size_t n) {
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

void start_camera(struct Camera *camera) {
    for (size_t i = 0; i < camera->n_buffers; ++i) {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(struct v4l2_buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        ioctl(camera->fd, VIDIOC_QBUF, &buffer);
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(camera->fd, VIDIOC_STREAMON, &type);
}

void get_frame(struct Camera *camera, struct Frame *frame) {
    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(struct v4l2_buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    ioctl(camera->fd, VIDIOC_DQBUF, &buffer);

    frame->data = malloc(buffer.bytesused);
    frame->size = buffer.bytesused;
    memcpy(frame->data, camera->buffers[buffer.index].data, buffer.bytesused);

    ioctl(camera->fd, VIDIOC_QBUF, &buffer);
}

void stop_camera(struct Camera *camera) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(camera->fd, VIDIOC_STREAMOFF, &type);
}

void close_camera(struct Camera *camera) {
    for (int i = 0; i < camera->n_buffers; ++i) {
        munmap(camera->buffers[i].data, camera->buffers[i].b_size);
    }
    close(camera->fd);
}

void encode(AVCodecContext *codec_contex, AVFrame *frame, AVPacket *packet, AVFormatContext *);
void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt);

int main() {
    const char *output_filename = "mwe_video.mp4";
    av_log_set_level(AV_LOG_WARNING);

    size_t n_frames = 300;
    struct Frame *frames = calloc(n_frames, sizeof(struct Frame));

    {
        struct Camera camera;
        open_camera(&camera, "/dev/video0");
        init_buffers(&camera, 2);
        start_camera(&camera);

        for (int i = 0; i < n_frames; ++i) {
            get_frame(&camera, frames + i);
            if ((i+1) % 10 == 0) { printf("Filming progress: %5.2f%%\n", (i+1)*100.0/n_frames); }
        }

        stop_camera(&camera);
        close_camera(&camera);
    }

    {
        AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        AVCodecContext *codec_context = avcodec_alloc_context3(codec);

        /* set codec parameters */
        codec_context->bit_rate = 400000;
        codec_context->width = 640;
        codec_context->height = 480;
        codec_context->time_base = (AVRational){300, 1};
        codec_context->framerate = (AVRational){1, 300};

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

        AVCodecParameters *codec_params = avcodec_parameters_alloc();
        avcodec_parameters_from_context(codec_params, codec_context);

        struct AVFormatContext *output_format_context = NULL;
        avformat_alloc_output_context2(&output_format_context, NULL, "mp4", output_filename);

        AVStream *out_video_stream = avformat_new_stream(output_format_context, NULL);

        /*     WHAT SHOULD I DO ???    */
        avcodec_parameters_from_context(out_video_stream->codecpar, codec_context);
        out_video_stream->time_base = codec_context->time_base;
        out_video_stream->avg_frame_rate = codec_context->framerate;
        /* =========================== */

        if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_open(&output_format_context->pb, output_filename, AVIO_FLAG_WRITE);
        }

        avformat_write_header(output_format_context, NULL);

        AVFrame *av_frame = av_frame_alloc();
        av_frame->format = codec_context->pix_fmt;
        av_frame->width  = codec_context->width;
        av_frame->height = codec_context->height;

        AVPacket *av_packet = av_packet_alloc();

        // magic align=32
        av_frame_get_buffer(av_frame, 32);

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

            encode(codec_context, av_frame, av_packet, output_format_context);
            if ((i+1) % 10 == 0) { printf("Encoding progress: %5.2f%%\n", (i+1)*100.0/n_frames); }
        }

        encode(codec_context, NULL, av_packet, output_format_context);

        av_write_trailer(output_format_context);

        if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_format_context->pb);
        }

        avformat_free_context(output_format_context);
        av_frame_free(&av_frame);
        av_packet_free(&av_packet);
        avcodec_parameters_free(&codec_params);
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
    AVFormatContext *output_format_context) {
    int ret;

    ret = avcodec_send_frame(codec_context, frame);

    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return;

        log_packet(output_format_context, packet);
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
