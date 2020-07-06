/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat API example.
 *
 * Output a media file in any supported libavformat format. The default
 * codecs are used.
 * @example muxing.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */

#define SCALE_FLAGS SWS_BICUBIC

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *av_stream;
    AVCodecContext *av_codec_context;

    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;

    AVFrame *frame;
    AVFrame *tmp_frame;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *packet)
{
    AVRational *time_base = &fmt_ctx->streams[packet->stream_index]->time_base;

    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(packet->pts), av_ts2timestr(packet->pts, time_base),
           av_ts2str(packet->dts), av_ts2timestr(packet->dts, time_base),
           av_ts2str(packet->duration), av_ts2timestr(packet->duration, time_base),
           packet->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, AVCodecContext *codec_context,
                       AVStream *av_stream, AVFrame *frame)
{
    int ret;

    // send the frame to the encoder
    ret = avcodec_send_frame(codec_context, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n",
                av_err2str(ret));
        exit(1);
    }

    while (ret >= 0) {
        AVPacket packet = { 0 };

        ret = avcodec_receive_packet(codec_context, &packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding a frame: %s\n", av_err2str(ret));
            exit(1);
        }

        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(&packet, codec_context->time_base, av_stream->time_base);
        packet.stream_index = av_stream->index;

        /* Write the compressed frame to the media file. */
        log_packet(fmt_ctx, &packet);
        ret = av_interleaved_write_frame(fmt_ctx, &packet);
        av_packet_unref(&packet);
        if (ret < 0) {
            fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(ret));
            exit(1);
        }
    }

    return ret == AVERROR_EOF ? 1 : 0;
}

/* Add an output stream. */
static void add_stream(OutputStream *ostream, AVFormatContext *format_context,
                       AVCodec **codec,
                       enum AVCodecID codec_id)
{
    AVCodecContext *codec_context;
    int i;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }


    ostream->av_stream = avformat_new_stream(format_context, NULL);
    if (!ostream->av_stream) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }

    ostream->av_stream->id = format_context->nb_streams-1;
    codec_context = avcodec_alloc_context3(*codec);
    if (!codec_context) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ostream->av_codec_context = codec_context;

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        codec_context->sample_fmt  = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        codec_context->bit_rate    = 64000;
        codec_context->sample_rate = 44100;
        if ((*codec)->supported_samplerates) {
            codec_context->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == 44100)
                    codec_context->sample_rate = 44100;
            }
        }
        codec_context->channels        = av_get_channel_layout_nb_channels(codec_context->channel_layout);
        codec_context->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
            codec_context->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    codec_context->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        codec_context->channels        = av_get_channel_layout_nb_channels(codec_context->channel_layout);
        ostream->av_stream->time_base = (AVRational){ 1, codec_context->sample_rate };
        break;

    case AVMEDIA_TYPE_VIDEO:
        codec_context->codec_id = codec_id;

        codec_context->bit_rate = 400000;
        /* Resolution must be a multiple of two. */
        codec_context->width    = 352;
        codec_context->height   = 288;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ostream->av_stream->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
        codec_context->time_base       = ostream->av_stream->time_base;

        codec_context->gop_size      = 12; /* emit one intra frame every twelve frames at most */
        codec_context->pix_fmt       = STREAM_PIX_FMT;
        if (codec_context->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B-frames */
            codec_context->max_b_frames = 2;
        }
        if (codec_context->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            codec_context->mb_decision = 2;
        }
    break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (format_context->oformat->flags & AVFMT_GLOBALHEADER)
        codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* audio output */

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  uint64_t channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame) {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }

    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }

    return frame;
}

static void open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ostream, AVDictionary *opt_arg)
{
    AVCodecContext *codec_context;
    int nb_samples;
    int ret;
    AVDictionary *opt = NULL;

    codec_context = ostream->av_codec_context;

    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(codec_context, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* init signal generator */
    ostream->t     = 0;
    ostream->tincr = 2 * M_PI * 110.0 / codec_context->sample_rate;
    /* increment frequency by 110 Hz per second */
    ostream->tincr2 = 2 * M_PI * 110.0 / codec_context->sample_rate / codec_context->sample_rate;

    if (codec_context->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = codec_context->frame_size;

    ostream->frame     = alloc_audio_frame(codec_context->sample_fmt, codec_context->channel_layout,
                                       codec_context->sample_rate, nb_samples);
    ostream->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, codec_context->channel_layout,
                                       codec_context->sample_rate, nb_samples);

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ostream->av_stream->codecpar, codec_context);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }

    /* create resampler context */
        ostream->swr_ctx = swr_alloc();
        if (!ostream->swr_ctx) {
            fprintf(stderr, "Could not allocate resampler context\n");
            exit(1);
        }

        /* set options */
        av_opt_set_int       (ostream->swr_ctx, "in_channel_count",   codec_context->channels,       0);
        av_opt_set_int       (ostream->swr_ctx, "in_sample_rate",     codec_context->sample_rate,    0);
        av_opt_set_sample_fmt(ostream->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
        av_opt_set_int       (ostream->swr_ctx, "out_channel_count",  codec_context->channels,       0);
        av_opt_set_int       (ostream->swr_ctx, "out_sample_rate",    codec_context->sample_rate,    0);
        av_opt_set_sample_fmt(ostream->swr_ctx, "out_sample_fmt",     codec_context->sample_fmt,     0);

        /* initialize the resampling context */
        if ((ret = swr_init(ostream->swr_ctx)) < 0) {
            fprintf(stderr, "Failed to initialize the resampling context\n");
            exit(1);
        }
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
static AVFrame *get_audio_frame(OutputStream *ostream)
{
    AVFrame *frame = ostream->tmp_frame;
    int j, i, v;
    int16_t *q = (int16_t*)frame->data[0];

    /* check if we want to generate more frames */
    if (av_compare_ts(ostream->next_pts, ostream->av_codec_context->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) > 0)
        return NULL;

    for (j = 0; j < frame->nb_samples; j++) {
        v = (int)(sin(ostream->t) * 10000);
        for (i = 0; i < ostream->av_codec_context->channels; i++)
            *q++ = v;
        ostream->t     += ostream->tincr;
        ostream->tincr += ostream->tincr2;
    }

    frame->pts = ostream->next_pts;
    ostream->next_pts  += frame->nb_samples;

    return frame;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_audio_frame(AVFormatContext *format_context, OutputStream *ostream)
{
    AVCodecContext *codec_context;
    AVFrame *frame;
    int ret;
    int dst_nb_samples;

    codec_context = ostream->av_codec_context;

    frame = get_audio_frame(ostream);

    if (frame) {
        /* convert samples from native format to destination codec format, using the resampler */
            /* compute destination number of samples */
            dst_nb_samples = av_rescale_rnd(swr_get_delay(ostream->swr_ctx, codec_context->sample_rate) + frame->nb_samples,
                                            codec_context->sample_rate, codec_context->sample_rate, AV_ROUND_UP);
            av_assert0(dst_nb_samples == frame->nb_samples);

        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(ostream->frame);
        if (ret < 0)
            exit(1);

        /* convert to destination format */
        ret = swr_convert(ostream->swr_ctx,
                          ostream->frame->data, dst_nb_samples,
                          (const uint8_t **)frame->data, frame->nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            exit(1);
        }
        frame = ostream->frame;

        frame->pts = av_rescale_q(ostream->samples_count, (AVRational){1, codec_context->sample_rate}, codec_context->time_base);
        ostream->samples_count += dst_nb_samples;
    }

    return write_frame(format_context, codec_context, ostream->av_stream, frame);
}

/**************************************************************/
/* video output */

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ostream, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *codec_context = ostream->av_codec_context;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    /* open the codec */
    ret = avcodec_open2(codec_context, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* allocate and init a re-usable frame */
    ostream->frame = alloc_picture(codec_context->pix_fmt, codec_context->width, codec_context->height);
    if (!ostream->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ostream->tmp_frame = NULL;
    if (codec_context->pix_fmt != AV_PIX_FMT_YUV420P) {
        ostream->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, codec_context->width, codec_context->height);
        if (!ostream->tmp_frame) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ostream->av_stream->codecpar, codec_context);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

/* Prepare a dummy image. */
static void fill_yuv_image(AVFrame *pict, int frame_index,
                           int width, int height)
{
    int x, y, i;

    i = frame_index;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}

static AVFrame *get_video_frame(OutputStream *ostream)
{
    AVCodecContext *codec_context = ostream->av_codec_context;

    /* check if we want to generate more frames */
    if (av_compare_ts(ostream->next_pts, codec_context->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) > 0)
        return NULL;

    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally; make sure we do not overwrite it here */
    if (av_frame_make_writable(ostream->frame) < 0)
        exit(1);

    if (codec_context->pix_fmt != AV_PIX_FMT_YUV420P) {
        /* as we only generate a YUV420P picture, we must convert it
         * to the codec pixel format if needed */
        if (!ostream->sws_ctx) {
            ostream->sws_ctx = sws_getContext(codec_context->width, codec_context->height,
                                          AV_PIX_FMT_YUV420P,
                                          codec_context->width, codec_context->height,
                                          codec_context->pix_fmt,
                                          SCALE_FLAGS, NULL, NULL, NULL);
            if (!ostream->sws_ctx) {
                fprintf(stderr,
                        "Could not initialize the conversion context\n");
                exit(1);
            }
        }
        fill_yuv_image(ostream->tmp_frame, ostream->next_pts, codec_context->width, codec_context->height);
        sws_scale(ostream->sws_ctx, (const uint8_t * const *) ostream->tmp_frame->data,
                  ostream->tmp_frame->linesize, 0, codec_context->height, ostream->frame->data,
                  ostream->frame->linesize);
    } else {
        fill_yuv_image(ostream->frame, ostream->next_pts, codec_context->width, codec_context->height);
    }

    ostream->frame->pts = ostream->next_pts++;

    return ostream->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVFormatContext *format_context, OutputStream *ostream)
{
    return write_frame(format_context, ostream->av_codec_context, ostream->av_stream, get_video_frame(ostream));

}

static void close_stream(AVFormatContext *format_context, OutputStream *ostream)
{
    avcodec_free_context(&ostream->av_codec_context);
    av_frame_free(&ostream->frame);
    av_frame_free(&ostream->tmp_frame);
    sws_freeContext(ostream->sws_ctx);
    swr_free(&ostream->swr_ctx);
}

/**************************************************************/
/* media file output */

int main(int argc, char **argv)
{
    OutputStream video_st = { 0 }, audio_st = { 0 };
    const char *filename;
    AVOutputFormat *av_output_format;
    AVFormatContext *av_format_context;
    AVCodec *audio_codec, *video_codec;
    AVDictionary *opt = NULL;

    int ret, i;
    int have_video = 0, have_audio = 0;
    int encode_video = 0, encode_audio = 0;

    if (argc < 2) {
        printf("usage: %s output_file\n"
               "API example program to output a media file with libavformat.\n"
               "This program generates a synthetic audio and video stream, encodes and\n"
               "muxes them into a file named output_file.\n"
               "The output format is automatically guessed according to the file extension.\n"
               "Raw images can also be output by using '%%d' in the filename.\n"
               "\n", argv[0]);
        return 1;
    }

    filename = argv[1];
    for (i = 2; i+1 < argc; i+=2) {
        if (!strcmp(argv[i], "-flags") || !strcmp(argv[i], "-fflags"))
            av_dict_set(&opt, argv[i]+1, argv[i+1], 0);
    }

    /* allocate the output media context */
    avformat_alloc_output_context2(&av_format_context, NULL, NULL, filename);
    if (!av_format_context) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&av_format_context, NULL, "mpeg", filename);
    }
    if (!av_format_context)
        return 1;

    av_output_format = av_format_context->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (av_output_format->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&video_st, av_format_context, &video_codec, av_output_format->video_codec);
        have_video = 1;
        encode_video = 1;
    }
    if (av_output_format->audio_codec != AV_CODEC_ID_NONE) {
        add_stream(&audio_st, av_format_context, &audio_codec, av_output_format->audio_codec);
        have_audio = 1;
        encode_audio = 1;
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (have_video) open_video(av_format_context, video_codec, &video_st, opt);
    if (have_audio) open_audio(av_format_context, audio_codec, &audio_st, opt);

    av_dump_format(av_format_context, 0, filename, 1);

    /* open the output file, if needed */
    if (!(av_output_format->flags & AVFMT_NOFILE)) {
        ret = avio_open(&av_format_context->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            return 1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(av_format_context, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        return 1;
    }

    while (encode_video || encode_audio) {
        /* select the stream to encode */
        if (encode_video &&
            (!encode_audio || av_compare_ts(video_st.next_pts, video_st.av_codec_context->time_base,
                                            audio_st.next_pts, audio_st.av_codec_context->time_base) <= 0)) {
            encode_video = !write_video_frame(av_format_context, &video_st);
        } else {
            encode_audio = !write_audio_frame(av_format_context, &audio_st);
        }
    }

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(av_format_context);

    /* Close each codec. */
    if (have_video) close_stream(av_format_context, &video_st);
    if (have_audio) close_stream(av_format_context, &audio_st);

    if (!(av_output_format->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&av_format_context->pb);

    /* free the stream */
    avformat_free_context(av_format_context);

    return 0;
}
