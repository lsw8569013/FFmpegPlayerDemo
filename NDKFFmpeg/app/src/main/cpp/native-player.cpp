#include <jni.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

/**
 * 1、遇到播放最后的时候，会闪退
 * 2、增加延迟计算的播放代码
 * 3、NDK定位错误的方法：addr2line工具
 *
 *  E:\Eclipse\android-studio-sdk\android-sdk-windows\ndk-bundle\toolchains\arm-linu
 *  x-androideabi-4.9\prebuilt\windows-x86_64\bin>arm-linux-androideabi-addr2line -e
 *  D:\workspace6\NDKFFmpeg\app\build\intermediates\cmake\debug\obj\armeabi\libnativ
 *  e-player.so 00001e94
 *
 *  D:\workspace6\NDKFFmpeg\app\src\main\cpp/native-player.cpp:225
 */

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libyuv/libyuv.h"
}

#include "queue.h"

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"ffmpeg",FORMAT,##__VA_ARGS__);

//队列的大小
#define PACKET_QUEUE_SIZE 50
//16bit 44100 PCM 数据大小
#define MAX_AUDIO_FRME_SIZE 44100 * 2
//视频文件中存在，音频流，视频流，字幕流，这里不测试字幕
#define MAX_STREAM 2

typedef struct _Player Player;
typedef struct _DecoderData DecoderData;

struct _Player {
    //虚拟机
    JavaVM *javaVM;
    //封装格式上下文
    AVFormatContext *input_format_ctx;
    //音频视频流索引位置
    int video_stream_index;
    int audio_stream_index;
    //流的总个数
    int captrue_streams_no;
    //解码器上下文数组
    AVCodecContext *input_codec_ctx[MAX_STREAM];
    //解码线程ID
    pthread_t decode_threads[MAX_STREAM];
    //surface输出窗口
    ANativeWindow *nativeWindow;
    //重采样上下文
    SwrContext *swr_ctx;
    //输入的采样格式
    enum AVSampleFormat in_sample_fmt;
    //输出采样格式16bit PCM
    enum AVSampleFormat out_sample_fmt;
    //输入采样率
    int in_sample_rate;
    //输出采样率
    int out_sample_rate;
    //输出的声道个数
    int out_channel_nb;
    //JNI
    jobject audio_track;
    jmethodID audio_track_write_mid;

    pthread_t thread_read_from_stream;
    //音频，视频队列数组
    Queue *packets[MAX_STREAM];

    //互斥锁
    pthread_mutex_t mutex;
    //条件变量
    pthread_cond_t cond;
};

/**
 * 解码数据
 */
struct _DecoderData {
    Player *player;
    int stream_index;
};

/**
 * 初始化封装格式上下文，获取音频视频流的索引位置
 */
void init_input_format_ctx(Player *player, const char *input_cstr) {
    //1、注册所有组件
    av_register_all();
    //封装格式上下文
    AVFormatContext *format_ctx = avformat_alloc_context();
    //2、打开视频文件
    if (avformat_open_input(&format_ctx, input_cstr, NULL, NULL) != 0) {
        LOGE("Cannot open input file");
        return;
    }
    //3、获取视频信息
    if (avformat_find_stream_info(format_ctx, NULL) < 0) {
        LOGE("Cannot find stream\n");
        return;
    }
    player->captrue_streams_no = format_ctx->nb_streams;
    LOGE("captrue_streams_no:%d", player->captrue_streams_no);
    //4、获取音频和视频流的索引位置
    int i;
    for (i = 0; i < player->captrue_streams_no; i++) {
        if (format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            player->video_stream_index = i;
        } else if (format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            player->audio_stream_index = i;
        }
    }
    player->input_format_ctx = format_ctx;
}

/**
 * 初始化解码器上下文
 */
void init_codec_context(Player *player, int stream_idx) {
    AVFormatContext *format_ctx = player->input_format_ctx;
    //获取解码器
    AVCodecContext *codec_ctx = format_ctx->streams[stream_idx]->codec;
    AVCodec *codec = avcodec_find_decoder(codec_ctx->codec_id);
    if (codec == NULL) {
        LOGE("%s", "无法解码");
        return;
    }
    //打开解码器
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        LOGE("%s", "解码器无法打开");
        return;
    }
    player->input_codec_ctx[stream_idx] = codec_ctx;
}

/**
 * 视频解码准备
 */
void decode_video_prepare(JNIEnv *env, Player *player, jobject surface) {
    player->nativeWindow = ANativeWindow_fromSurface(env, surface);
}

/**
 * 音频解码准备
 */
void decode_audio_prepare(Player *player) {
    AVCodecContext *codec_ctx = player->input_codec_ctx[player->audio_stream_index];
    //输入的采样格式
    enum AVSampleFormat in_sample_fmt = codec_ctx->sample_fmt;
    //输出采样格式16bit PCM
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    //输入采样率
    int in_sample_rate = codec_ctx->sample_rate;
    //输出采样率
    int out_sample_rate = in_sample_rate;
    //获取输入的声道布局
    uint64_t in_ch_layout = codec_ctx->channel_layout;
    //输出的声道布局（立体声）
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;

    //frame->16bit 44100 PCM 统一音频采样格式与采样率
    SwrContext *swr_ctx = swr_alloc();
    //重采样设置参数
    swr_alloc_set_opts(swr_ctx,
                       out_ch_layout, out_sample_fmt, out_sample_rate,
                       in_ch_layout, in_sample_fmt, in_sample_rate,
                       0, NULL);
    swr_init(swr_ctx);
    //输出的声道个数
    int out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);

    player->in_sample_fmt = in_sample_fmt;
    player->out_sample_fmt = out_sample_fmt;
    player->in_sample_rate = in_sample_rate;
    player->out_sample_rate = out_sample_rate;
    player->out_channel_nb = out_channel_nb;
    player->swr_ctx = swr_ctx;
}

/**
 * 初始化JNI
 */
void jni_audio_prepare(JNIEnv *env, jobject jthiz, Player *player) {
    //JNI begin------------------
    //JasonPlayer
    jclass player_class = env->GetObjectClass(jthiz);
    //AudioTrack对象
    jmethodID create_audio_track_mid = env->GetMethodID(player_class, "createAudioTrack",
                                                        "(I)Landroid/media/AudioTrack;");
    jobject audio_track = env->CallObjectMethod(jthiz, create_audio_track_mid,
                                                player->out_sample_rate, player->out_channel_nb);
    //调用AudioTrack.play方法
    jclass audio_track_class = env->GetObjectClass(audio_track);
    jmethodID audio_track_play_mid = env->GetMethodID(audio_track_class, "play", "()V");
    env->CallVoidMethod(audio_track, audio_track_play_mid);
    //AudioTrack.write
    jmethodID audio_track_write_mid = env->GetMethodID(audio_track_class, "write", "([BII)I");
    //JNI end------------------
    player->audio_track = env->NewGlobalRef(audio_track);
    //env->DeleteGlobalRef
    player->audio_track_write_mid = audio_track_write_mid;
}

/**
 * 给AVPacket开辟空间，后面会将AVPacket栈内存数据拷贝至这里开辟的空间
 */
void *player_fill_packet() {
    //请参照我在vs中写的代码
    AVPacket *packet = (AVPacket *) malloc(sizeof(AVPacket));
    return packet;
}

/**
 * 初始化音频，视频AVPacket队列，长度50
 */
void player_alloc_queues(Player *player) {
    int i;
    //这里，正常是初始化两个队列
    for (i = 0; i < player->captrue_streams_no; ++i) {
        Queue *queue = queue_init(PACKET_QUEUE_SIZE, (queue_fill_func) player_fill_packet);
        player->packets[i] = queue;
        //打印视频音频队列地址
        LOGE("stream index:%d,queue:%#x", i, queue);
    }
}

/**
 * 生产者线程：负责不断的读取视频文件中AVPacket，分别放入两个队列中
 */
void *player_read_from_stream(void *arg) {
    int index = 0;
    int ret;
    Player *player = (Player *) arg;
    //栈内存上保存一个AVPacket
    AVPacket packet, *pkt = &packet;
    for (;;) {
        ret = av_read_frame(player->input_format_ctx, pkt);
        //到文件结尾了，这里有个bug
        if (ret < 0) {
//            sleep(1000);
            LOGE("到达文件末尾-----------------------------");
            pthread_detach(player->thread_read_from_stream);
            break;
        }

        if(ret == 0){

            //根据AVpacket->stream_index获取对应的队列
            Queue *queue = player->packets[pkt->stream_index];
            //示范队列内存释放
            //queue_free(queue,packet_free_func);
            pthread_mutex_lock(&player->mutex);
            //将AVPacket压入队列
            AVPacket *packet_data = (AVPacket *) queue_push(queue, &player->mutex, &player->cond);
            //拷贝（间接赋值，拷贝结构体数据）
            *packet_data = packet;
            pthread_mutex_unlock(&player->mutex);
//            LOGE("queue---:%#x, packet:%#x", queue, packet);
        }
    }
}


/**
 * 解码视频
 */
void decode_video(Player *player, AVPacket *packet) {
    //像素数据（解码数据）
    AVFrame *yuv_frame = av_frame_alloc();
    AVFrame *rgb_frame = av_frame_alloc();
    //绘制时的缓冲区
    ANativeWindow_Buffer outBuffer;
    AVCodecContext *codec_ctx = player->input_codec_ctx[player->video_stream_index];
    int got_frame;
    //解码AVPacket->AVFrame
    avcodec_decode_video2(codec_ctx, yuv_frame, &got_frame, packet);
    //Zero if no frame could be decompressed
    //非零，正在解码
    if (got_frame) {
        //lock
        //设置缓冲区的属性（宽、高、像素格式）
        ANativeWindow_setBuffersGeometry(player->nativeWindow, codec_ctx->width, codec_ctx->height,
                                         WINDOW_FORMAT_RGBA_8888);
        ANativeWindow_lock(player->nativeWindow, &outBuffer, NULL);

        //设置rgb_frame的属性（像素格式、宽高）和缓冲区
        //rgb_frame缓冲区与outBuffer.bits是同一块内存
        avpicture_fill((AVPicture *) rgb_frame, (const uint8_t *) outBuffer.bits, AV_PIX_FMT_RGBA,
                       codec_ctx->width, codec_ctx->height);

        //YUV->RGBA_8888
        libyuv::I420ToARGB(yuv_frame->data[0], yuv_frame->linesize[0],
                           yuv_frame->data[2], yuv_frame->linesize[2],
                           yuv_frame->data[1], yuv_frame->linesize[1],
                           rgb_frame->data[0], rgb_frame->linesize[0],
                           codec_ctx->width, codec_ctx->height);

        //unlock
        ANativeWindow_unlockAndPost(player->nativeWindow);
        usleep(1000 * 16);
    }
    av_frame_free(&yuv_frame);
    av_frame_free(&rgb_frame);
}

/**
 * 音频解码
 */
void decode_audio(Player *player, AVPacket *packet) {
    AVCodecContext *codec_ctx = player->input_codec_ctx[player->audio_stream_index];
    LOGE("%s", "decode_audio");
    //解压缩数据
    AVFrame *frame = av_frame_alloc();
    int got_frame;
    avcodec_decode_audio4(codec_ctx, frame, &got_frame, packet);

    //16bit 44100 PCM 数据（重采样缓冲区）
    uint8_t *out_buffer = (uint8_t *) av_malloc(MAX_AUDIO_FRME_SIZE);
    //解码一帧成功
    if (got_frame > 0) {
        swr_convert(player->swr_ctx, &out_buffer, MAX_AUDIO_FRME_SIZE,
                    (const uint8_t **) frame->data, frame->nb_samples);
        //获取sample的size
        int out_buffer_size = av_samples_get_buffer_size(NULL, player->out_channel_nb,
                                                         frame->nb_samples, player->out_sample_fmt,
                                                         1);

        //关联当前线程的JNIEnv
        JavaVM *javaVM = player->javaVM;
        JNIEnv *env;
        javaVM->AttachCurrentThread(&env, NULL);
        //out_buffer缓冲区数据，转成byte数组
        jbyteArray audio_sample_array = env->NewByteArray(out_buffer_size);
        jbyte *sample_bytep = env->GetByteArrayElements(audio_sample_array, NULL);
        //out_buffer的数据复制到sampe_bytep
        memcpy(sample_bytep, out_buffer, out_buffer_size);
        //同步
        env->ReleaseByteArrayElements(audio_sample_array, sample_bytep, 0);

        //AudioTrack.write PCM数据
        env->CallIntMethod(player->audio_track, player->audio_track_write_mid,
                           audio_sample_array, 0, out_buffer_size);
        //释放局部引用
        env->DeleteLocalRef(audio_sample_array);

        javaVM->DetachCurrentThread();

        usleep(1000 * 16);
    }

    av_frame_free(&frame);
}

/**
 * 解码子线程函数（消费）
 */
void *decode_data(void *arg) {
    DecoderData *decoder_data = (DecoderData *) arg;
    Player *player = decoder_data->player;
    int stream_index = decoder_data->stream_index;
    //根据stream_index获取对应的AVPacket队列
    Queue *queue = player->packets[stream_index];

    AVFormatContext *format_ctx = player->input_format_ctx;
    //编码数据

    //6.一阵一阵读取压缩的视频数据AVPacket
    int video_frame_count = 0, audio_frame_count = 0;
    for (;;) {
        //消费AVPacket
        pthread_mutex_lock(&player->mutex);
        AVPacket *packet = (AVPacket *) queue_pop(queue, &player->mutex, &player->cond);
        pthread_mutex_unlock(&player->mutex);
        if (stream_index == player->video_stream_index) {
            decode_video(player, packet);
            LOGE("video_frame_count:%d", video_frame_count++);
        } else if (stream_index == player->audio_stream_index) {
            decode_audio(player, packet);
            LOGE("audio_frame_count:%d", audio_frame_count++);
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_handsome_ndkffmpeg_FFmpegUtils_videoAndSoundPlay(JNIEnv *env, jobject instance,
                                                          jstring input_path_, jobject surface) {
    const char *input_path = env->GetStringUTFChars(input_path_, 0);
    Player *player = (Player *) malloc(sizeof(Player));
    env->GetJavaVM(&(player->javaVM));

    //初始化封装格式上下文
    init_input_format_ctx(player, input_path);
    int video_stream_index = player->video_stream_index;
    int audio_stream_index = player->audio_stream_index;
    //获取音视频解码器，并打开
    init_codec_context(player, video_stream_index);
    init_codec_context(player, audio_stream_index);

    //初始化音视频
    decode_video_prepare(env, player, surface);
    decode_audio_prepare(player);

    //初始化JNI
    jni_audio_prepare(env, instance, player);
    //初始化音视频AVPacket队列
    player_alloc_queues(player);

    pthread_mutex_init(&player->mutex, NULL);
    pthread_cond_init(&player->cond, NULL);

    //生产者线程
    pthread_create(&(player->thread_read_from_stream), NULL, player_read_from_stream,
                   (void *) player);

    //消费者线程
    DecoderData data1 = {player, video_stream_index}, *decoder_data1 = &data1;
    pthread_create(&(player->decode_threads[video_stream_index]), NULL, decode_data,
                   (void *) decoder_data1);

    DecoderData data2 = {player, audio_stream_index}, *decoder_data2 = &data2;
    pthread_create(&(player->decode_threads[audio_stream_index]), NULL, decode_data,
                   (void *) decoder_data2);

    pthread_join(player->thread_read_from_stream, NULL);
    pthread_join(player->decode_threads[video_stream_index], NULL);
    pthread_join(player->decode_threads[audio_stream_index], NULL);

    env->ReleaseStringUTFChars(input_path_, input_path);
}

