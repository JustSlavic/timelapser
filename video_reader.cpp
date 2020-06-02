#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#undef av_err2str
#define av_err2str(errnum) av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), AV_ERROR_MAX_STRING_SIZE, errnum)

static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename);


int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Please, provide video file!\n");
        exit(1);
    }

    AVFormatContext *pFormatCtx = nullptr;
    if (avformat_open_input(&pFormatCtx, argv[1], nullptr, nullptr) < 0) {
        fprintf(stderr, "Could not open file \"%s\".\n", argv[1]);
        exit(1);
    }

    printf("Format %s, duration: %ld us.\n", pFormatCtx->iformat->long_name, pFormatCtx->duration);

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        fprintf(stderr, "Could not find stream info.\n");
        exit(1);
    }

    // av_dump_format(pFormatCtx, 0, argv[1], 0);

    AVCodec *pCodec = nullptr;
    AVCodecParameters *pCodecParams = nullptr;

    int video_stream_id = -1;
    for (int i = 0; i < pFormatCtx->nb_streams; ++i) {
        AVCodecParameters *pLocalCodecParams = pFormatCtx->streams[i]->codecpar;

        if (pLocalCodecParams->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_id = i;
        }
    }

    if (video_stream_id == -1) {
        fprintf(stderr, "Could not find video stream\n");
        exit(1);
    }

    pCodecParams = pFormatCtx->streams[video_stream_id]->codecpar;
    pCodec = avcodec_find_decoder(pCodecParams->codec_id);

    if (pCodec == nullptr) {
        fprintf(stderr, "Could not find decoder\n");
        exit(1);
    }

    printf("Codec:\n"
           "    Name: %s\n"
           "    ID: %d\n"
           "    Resolution: %dx%d\n"
           "    Bit rate: %ld\n"
           "\n",
           pCodec->long_name, pCodec->id,
           pCodecParams->width, pCodecParams->height,
           pCodecParams->bit_rate);

    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    if (pCodecCtx == nullptr) {
        fprintf(stderr, "Could not allocate memory for codec context\n");
        exit(1);
    }

    if (avcodec_parameters_to_context(pCodecCtx, pCodecParams) < 0) {
        fprintf(stderr, "Could not fill in codec context from codec paramters\n");
        exit(1);
    }

    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        fprintf(stderr, "Could not open the given codec\n");
        exit(1);
    }

    AVFrame *pFrame = av_frame_alloc();
    if (pFrame == nullptr) {
        fprintf(stderr, "Could not allocate memory for a frame\n");
        exit(1);
    }

    AVPacket *pPacket = av_packet_alloc();
    if (pPacket == nullptr) {
        fprintf(stderr, "Could not allocate memory for a packet\n");
        exit(1);
    }

    int frames_left = 32;
    while (frames_left) {
        if (av_read_frame(pFormatCtx, pPacket) < 0) {
            fprintf(stderr, "Failed to read next frame because of error or EOF\n");
            break;
        }

        if (pPacket->stream_index == video_stream_id) {
            if (avcodec_send_packet(pCodecCtx, pPacket) < 0) {
                fprintf(stderr, "Failed to decode packet\n");
                exit(1);
            }

            while (true) {
                int err = avcodec_receive_frame(pCodecCtx, pFrame);
                if (err == AVERROR(EAGAIN)) { break; }
                if (err == AVERROR_EOF) {
                    frames_left = 0;
                    break;
                }
                if (err < 0) {
                    fprintf(stderr, "Failed to receive decoded frame: %s\n", av_err2str(err));
                    exit(1);
                }

                printf("Frame %d (type=%c, size=%d bytes) pts %ld key_frame %d [DTS %d]\n",
                       pCodecCtx->frame_number,
                       av_get_picture_type_char(pFrame->pict_type),
                       pFrame->pkt_size,
                       pFrame->pts,
                       pFrame->key_frame,
                       pFrame->coded_picture_number);

                char frame_filename[1024];
                snprintf(frame_filename, sizeof(frame_filename), "data/%s-%d.pgm", "frame", pCodecCtx->frame_number);
                save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, frame_filename);

                frames_left -= 1;
            }
        }

        av_packet_unref(pPacket);
    }


    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}


static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename,"w");
    // writing the minimal required header for a pgm file format
    // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

    // writing line by line
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}
