#include <jni.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <android/log.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libyuv/libyuv.h"
}

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"ffmpeg",FORMAT,##__VA_ARGS__);
//16bit 44100 PCM 数据大小
#define MAX_AUDIO_FRME_SIZE 44100 * 2

extern "C"
JNIEXPORT void JNICALL
Java_com_handsome_ndkffmpeg_FFmpegUtils_soundPlay(JNIEnv *env, jobject instance,
                                                  jstring input_path_) {
    const char *input_path = env->GetStringUTFChars(input_path_, 0);
    //1、注册所有组件
    av_register_all();
    //2、打开视频文件
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    if ((avformat_open_input(&pFormatCtx, input_path, NULL, NULL)) < 0) {
        LOGE("Cannot open input file");
        return;
    }
    //3、获取视频信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("Cannot find stream\n");
        return;
    }
    //4、找到视频流的位置
    int audio_stream_index = -1;
    int i = 0;
    for (; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            LOGE("find the stream index %d", audio_stream_index);
            break;
        }
    }
    //5、获取解码器
    AVCodecContext *pCodeCtx = pFormatCtx->streams[audio_stream_index]->codec;
    AVCodec *pCodec = avcodec_find_decoder(pCodeCtx->codec_id);
    if (pCodec == NULL) {
        LOGE("Cannot find decoder\n");
        return;
    }
    //6、打开解码器
    if (avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        LOGE("Cannot open codec\n");
        return;
    }
    //7、解析每一帧数据（包含重采样）
    int got_picture_ptr, frame_count = 1;
    //压缩数据
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    //解压缩数据
    AVFrame *frame = av_frame_alloc();

    //重采样设置参数，将frame数据转成16bit比特率44100的PCM格式
    //重采样上下文
    SwrContext *swrCtx = swr_alloc();
    //输入的采样格式
    enum AVSampleFormat in_sample_fmt = pCodeCtx->sample_fmt;
    //输出采样格式16bit的PCM
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    //输入采样率
    int in_sample_rate = pCodeCtx->sample_rate;
    //输出采样率
    int out_sample_rate = 44100;
    //获取输入的声道布局
    uint64_t in_ch_layout = pCodeCtx->channel_layout;
    //输出的声道布局（立体声）
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
    //设置重采样配置
    swr_alloc_set_opts(swrCtx,
                       out_ch_layout, out_sample_fmt, out_sample_rate,
                       in_ch_layout, in_sample_fmt, in_sample_rate,
                       0, NULL);
    //重采样初始化
    swr_init(swrCtx);
    //获取输出的声道个数
    int out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);
    //16bit 44100 PCM 数据大小
    uint8_t *out_buffer = (uint8_t *) av_malloc(MAX_AUDIO_FRME_SIZE);

    //在读取之前拿到AudioTrack
    //begin--JNI
    jclass player_class = env->GetObjectClass(instance);
    //获取AudioTrack对象
    jmethodID create_audio_track_mid = env->GetMethodID(player_class, "createAudioTrack",
                                                        "(I)Landroid/media/AudioTrack;");
    jobject audio_track = env->CallObjectMethod(instance, create_audio_track_mid, out_channel_nb);
    //调用AudioTrack.play方法
    jclass audio_track_class = env->GetObjectClass(audio_track);
    jmethodID audio_track_play_mid = env->GetMethodID(audio_track_class, "play", "()V");
    env->CallVoidMethod(audio_track, audio_track_play_mid);
    //获取AudioTrack.write
    jmethodID audio_track_write_mid = env->GetMethodID(audio_track_class, "write", "([BII)I");
    //end--JNI

    //一帧一帧读取压缩的视频数据
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        //找到音频流
        if (packet->stream_index == audio_stream_index) {
            avcodec_decode_audio4(pCodeCtx, frame, &got_picture_ptr, packet);
            //正在解码
            if (got_picture_ptr) {
                //重采样转换
                swr_convert(swrCtx, &out_buffer, MAX_AUDIO_FRME_SIZE,
                            (const uint8_t **) frame->data,
                            frame->nb_samples);
                //获取采样的大小
                int out_buffer_size = av_samples_get_buffer_size(NULL, out_channel_nb,
                                                                 frame->nb_samples, out_sample_fmt,
                                                                 1);
                //播放每一帧的音频
                //begin--JNI
                //out_buffer_size缓冲区数据，转成byte数组
                jbyteArray audio_sample_array = env->NewByteArray(out_buffer_size);
                jbyte *sample_byte = env->GetByteArrayElements(audio_sample_array, NULL);
                //out_buffer的数据复制到sample_byte
                memcpy(sample_byte, out_buffer, out_buffer_size);
                //同步数据
                env->ReleaseByteArrayElements(audio_sample_array, sample_byte, 0);
                //调用AudioTrack.write
                env->CallIntMethod(audio_track, audio_track_write_mid,
                                   audio_sample_array, 0, out_buffer_size);
                //释放局部引用
                env->DeleteLocalRef(audio_sample_array);
                usleep(1000 * 16);
                //end--JNI
                LOGE("解析第%d帧", (frame_count++));
            }
            av_free_packet(packet);
        }
    }
    //8、释放资源
    av_frame_free(&frame);
    av_free(out_buffer);
    swr_free(&swrCtx);
    avcodec_close(pCodeCtx);
    avformat_close_input(&pFormatCtx);
    env->ReleaseStringUTFChars(input_path_, input_path);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_handsome_ndkffmpeg_FFmpegUtils_sound2PCM(JNIEnv *env, jclass type, jstring input_path_,
                                                  jstring output_path_) {
    const char *input_path = env->GetStringUTFChars(input_path_, 0);
    const char *output_path = env->GetStringUTFChars(output_path_, 0);
    //1、注册所有组件
    av_register_all();
    //2、打开视频文件
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    if ((avformat_open_input(&pFormatCtx, input_path, NULL, NULL)) < 0) {
        LOGE("Cannot open input file");
        return;
    }
    //3、获取视频信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("Cannot find stream\n");
        return;
    }
    //4、找到视频流的位置
    int audio_stream_index = -1;
    int i = 0;
    for (; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            LOGE("find the stream index %d", audio_stream_index);
            break;
        }
    }
    //5、获取解码器
    AVCodecContext *pCodeCtx = pFormatCtx->streams[audio_stream_index]->codec;
    AVCodec *pCodec = avcodec_find_decoder(pCodeCtx->codec_id);
    if (pCodec == NULL) {
        LOGE("Cannot find decoder\n");
        return;
    }
    //6、打开解码器
    if (avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        LOGE("Cannot open codec\n");
        return;
    }
    //7、解析每一帧数据（包含重采样）
    int got_picture_ptr, frame_count = 1;
    //压缩数据
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    //解压缩数据
    AVFrame *frame = av_frame_alloc();

    //重采样设置参数，将frame数据转成16bit比特率44100的PCM格式
    //重采样上下文
    SwrContext *swrCtx = swr_alloc();
    //输入的采样格式
    enum AVSampleFormat in_sample_fmt = pCodeCtx->sample_fmt;
    //输出采样格式16bit的PCM
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    //输入采样率
    int in_sample_rate = pCodeCtx->sample_rate;
    //输出采样率
    int out_sample_rate = 44100;
    //获取输入的声道布局
    uint64_t in_ch_layout = pCodeCtx->channel_layout;
    //输出的声道布局（立体声）
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
    //设置重采样配置
    swr_alloc_set_opts(swrCtx,
                       out_ch_layout, out_sample_fmt, out_sample_rate,
                       in_ch_layout, in_sample_fmt, in_sample_rate,
                       0, NULL);
    //重采样初始化
    swr_init(swrCtx);
    //获取输出的声道个数
    int out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);
    //16bit 44100 PCM 数据
    uint8_t *out_buffer = (uint8_t *) av_malloc(MAX_AUDIO_FRME_SIZE);
    //输出文件
    FILE *fp_pcm = fopen(output_path, "wb");
    //一帧一帧读取压缩的视频数据
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        //找到音频流
        if (packet->stream_index == audio_stream_index) {
            avcodec_decode_audio4(pCodeCtx, frame, &got_picture_ptr, packet);
            //正在解码
            if (got_picture_ptr) {
                //重采样转换
                swr_convert(swrCtx, &out_buffer, MAX_AUDIO_FRME_SIZE,
                            (const uint8_t **) frame->data,
                            frame->nb_samples);
                //获取采样的大小
                int out_buffer_size = av_samples_get_buffer_size(NULL, out_channel_nb,
                                                                 frame->nb_samples, out_sample_fmt,
                                                                 1);
                fwrite(out_buffer, 1, out_buffer_size, fp_pcm);
                LOGE("解析第%d帧", (frame_count++));
            }
            av_free_packet(packet);
        }
    }
    //8、释放资源
    fclose(fp_pcm);
    av_frame_free(&frame);
    av_free(out_buffer);
    swr_free(&swrCtx);
    avcodec_close(pCodeCtx);
    avformat_close_input(&pFormatCtx);
    env->ReleaseStringUTFChars(input_path_, input_path);
    env->ReleaseStringUTFChars(output_path_, output_path);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_handsome_ndkffmpeg_FFmpegUtils_video2YUV(JNIEnv *env, jclass jclazz, jstring input_path_,
                                                  jstring out_path_) {
    const char *input_path = env->GetStringUTFChars(input_path_, NULL);
    const char *output_path = env->GetStringUTFChars(out_path_, NULL);
    //1、注册所有组件
    av_register_all();
    //2、打开视频文件
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    if ((avformat_open_input(&pFormatCtx, input_path, NULL, NULL)) < 0) {
        LOGE("Cannot open input file");
        return;
    }
    //3、获取视频信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("Cannot find stream\n");
        return;
    }
    //4、找到视频流的位置
    int video_stream_index = -1;
    int i = 0;
    for (; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            LOGE("find the stream index %d", video_stream_index);
            break;
        }
    }
    //5、获取解码器
    AVCodecContext *pCodeCtx = pFormatCtx->streams[video_stream_index]->codec;
    AVCodec *pCodec = avcodec_find_decoder(pCodeCtx->codec_id);
    if (pCodec == NULL) {
        LOGE("Cannot find decoder\n");
        return;
    }
    //6、打开解码器
    if (avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        LOGE("Cannot open codec\n");
        return;
    }
    //7、解析每一帧数据
    int got_picture_ptr, frame_count = 1;
    //压缩数据
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    //解压缩数据
    AVFrame *frame = av_frame_alloc();
    AVFrame *yuvFrame = av_frame_alloc();

    //将视频转换成指定的420P的YUV格式
    //缓冲区分配内存
    uint8_t *out_buffer = (uint8_t *) av_malloc(
            avpicture_get_size(AV_PIX_FMT_YUV420P, pCodeCtx->width, pCodeCtx->height));
    //初始化缓冲区
    avpicture_fill((AVPicture *) yuvFrame, out_buffer, AV_PIX_FMT_YUV420P, pCodeCtx->width,
                   pCodeCtx->height);
    //用于像素格式转换或者缩放
    struct SwsContext *sws_ctx = sws_getContext(
            pCodeCtx->width, pCodeCtx->height, pCodeCtx->pix_fmt,
            pCodeCtx->width, pCodeCtx->height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, NULL, NULL, NULL);
    //输出文件
    FILE *fp_yuv = fopen(output_path, "wb");
    //一帧一帧读取压缩的视频数据
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        //找到视频流
        if (packet->stream_index == video_stream_index) {
            avcodec_decode_video2(pCodeCtx, frame, &got_picture_ptr, packet);
            //正在解码
            if (got_picture_ptr) {
                //frame->yuvFrame，转为指定的YUV420P像素帧
                sws_scale(sws_ctx, (const uint8_t *const *) frame->data, frame->linesize, 0,
                          frame->height, yuvFrame->data, yuvFrame->linesize);
                //计算视频数据总大小
                int y_size = pCodeCtx->width * pCodeCtx->height;
                //AVFrame->YUV，由于YUV的比例是4:1:1
                fwrite(yuvFrame->data[0], 1, y_size, fp_yuv);
                fwrite(yuvFrame->data[1], 1, y_size / 4, fp_yuv);
                fwrite(yuvFrame->data[2], 1, y_size / 4, fp_yuv);
                LOGE("解析第%d帧", (frame_count++));
            }
            av_free_packet(packet);
        }
    }
    //8、释放资源
    fclose(fp_yuv);
    av_frame_free(&frame);
    avcodec_close(pCodeCtx);
    avformat_free_context(pFormatCtx);
    env->ReleaseStringUTFChars(input_path_, input_path);
    env->ReleaseStringUTFChars(out_path_, output_path);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_handsome_ndkffmpeg_FFmpegUtils_video2RGB(JNIEnv *env, jclass type, jstring input_path_,
                                                  jobject surface) {
    const char *input_path = env->GetStringUTFChars(input_path_, 0);
    //1、注册所有组件
    av_register_all();
    //2、打开视频文件
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    if ((avformat_open_input(&pFormatCtx, input_path, NULL, NULL)) < 0) {
        LOGE("Cannot open input file");
        return;
    }
    //3、获取视频信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("Cannot find stream\n");
        return;
    }
    //4、找到视频流的位置
    int video_stream_index = -1;
    int i = 0;
    for (; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            LOGE("find the stream index %d", video_stream_index);
            break;
        }
    }
    //5、获取解码器
    AVCodecContext *pCodeCtx = pFormatCtx->streams[video_stream_index]->codec;
    AVCodec *pCodec = avcodec_find_decoder(pCodeCtx->codec_id);
    if (pCodec == NULL) {
        LOGE("Cannot find decoder\n");
        return;
    }
    //6、打开解码器
    if (avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        LOGE("Cannot open codec\n");
        return;
    }
    //7、解析每一帧数据
    int got_picture_ptr, frame_count = 1;
    //压缩数据
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    //解压缩数据
    AVFrame *yuv_frame = av_frame_alloc();
    AVFrame *rgb_frame = av_frame_alloc();
    //绘制时的surface窗口
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    //绘制时的缓冲区
    ANativeWindow_Buffer outBuffer;
    //一帧一帧读取压缩的视频数据
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        //找到视频流
        if (packet->stream_index == video_stream_index) {
            avcodec_decode_video2(pCodeCtx, yuv_frame, &got_picture_ptr, packet);
            //正在解码
            if (got_picture_ptr) {
                LOGE("解码%d帧", frame_count++);
                //设置缓冲区的属性（宽、高、像素格式）
                ANativeWindow_setBuffersGeometry(window, pCodeCtx->width, pCodeCtx->height,
                                                 WINDOW_FORMAT_RGBA_8888);
                ANativeWindow_lock(window, &outBuffer, NULL);
                //设置rgb_frame的属性（像素格式、宽高）和缓冲区
                //rgb_frame缓冲区与outBuffer.bits是同一块内存
                avpicture_fill((AVPicture *) rgb_frame, (const uint8_t *) outBuffer.bits,
                               PIX_FMT_RGBA, pCodeCtx->width, pCodeCtx->height);
                //YUV->RGBA_8888
                libyuv::I420ToARGB(yuv_frame->data[0], yuv_frame->linesize[0],
                                   yuv_frame->data[2], yuv_frame->linesize[2],
                                   yuv_frame->data[1], yuv_frame->linesize[1],
                                   rgb_frame->data[0], rgb_frame->linesize[0],
                                   pCodeCtx->width, pCodeCtx->height);
                //unlock
                ANativeWindow_unlockAndPost(window);
                //绘制停顿16ms
                usleep(1000 * 16);
            }
            av_free_packet(packet);
        }
    }
    //8、释放资源
    ANativeWindow_release(window);
    av_frame_free(&yuv_frame);
    avcodec_close(pCodeCtx);
    avformat_free_context(pFormatCtx);
    env->ReleaseStringUTFChars(input_path_, input_path);
}
