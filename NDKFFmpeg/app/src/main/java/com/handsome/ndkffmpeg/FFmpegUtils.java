package com.handsome.ndkffmpeg;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.util.Log;
import android.view.Surface;

/**
 * =====作者=====
 * 许英俊
 * =====时间=====
 * 2017/9/5.
 */

public class FFmpegUtils {

    static {
        System.loadLibrary("avutil-54");
        System.loadLibrary("swresample-1");
        System.loadLibrary("avcodec-56");
        System.loadLibrary("avformat-56");
        System.loadLibrary("swscale-3");
        System.loadLibrary("postproc-53");
        System.loadLibrary("avfilter-5");
        System.loadLibrary("avdevice-56");
        System.loadLibrary("yuv");
        System.loadLibrary("native-lib");
        System.loadLibrary("native-player");
    }

    /**
     * 视频转换输出YUV格式文件
     *
     * @param input_path
     * @param output_path
     */
    public static native void video2YUV(String input_path, String output_path);

    /**
     * 视频转换显示RGB格式
     *
     * @param video_path
     * @param surface
     */
    public static native void video2RGB(String video_path, Surface surface);

    /**
     * 音频转换输出PCM文件
     *
     * @param input_path
     * @param output_path
     */
    public static native void sound2PCM(String input_path, String output_path);


    /**
     * 播放音频
     *
     * @param input_path
     */
    public native void soundPlay(String input_path);

    /**
     * 播放音视频
     *
     * @param input_path
     * @param surface
     */
    public native void videoAndSoundPlay(String input_path, Surface surface);


    /**
     * 创建一个AudioTrack对象，用于播放
     *
     * @param nb_channels
     * @return
     */
    public AudioTrack createAudioTrack(int nb_channels) {
        //固定的比特率
        int sampleRateInHz = 44100;
        //固定格式的音频码流
        int audioFormat = AudioFormat.ENCODING_PCM_16BIT;
        //声道布局
        int channelConfig;
        if (nb_channels == 1) {
            channelConfig = android.media.AudioFormat.CHANNEL_OUT_MONO;
        } else if (nb_channels == 2) {
            channelConfig = android.media.AudioFormat.CHANNEL_OUT_STEREO;
        } else {
            channelConfig = android.media.AudioFormat.CHANNEL_OUT_STEREO;
        }
        int bufferSizeInBytes = AudioTrack.getMinBufferSize(sampleRateInHz, channelConfig, audioFormat);

        AudioTrack audioTrack = new AudioTrack(AudioManager.STREAM_MUSIC, sampleRateInHz, channelConfig,
                audioFormat, bufferSizeInBytes, AudioTrack.MODE_STREAM);
        return audioTrack;
    }

}
