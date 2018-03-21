package com.handsome.ndkffmpeg;

import android.os.Environment;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Toast;

import java.io.File;

/**
 * ffmpeg 学习demo
 */
public class MainActivity extends AppCompatActivity {

    VideoView videoView;
    String input;
    String output;
    private File inputFile;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        videoView = (VideoView) findViewById(R.id.video_view);
        //资源在MP4 文件夹 下
//        inputFile = new File(Environment.getExternalStorageDirectory(), "decode.avi");
        inputFile = new File(Environment.getExternalStorageDirectory(), "Titanic.mkv");

        input = inputFile.getAbsolutePath();
    }

    public void video2YUV(View view) {
        output = new File(Environment.getExternalStorageDirectory(), "decode.yuv").getAbsolutePath();
        //视频转换成YUV格式
        FFmpegUtils.video2YUV(input, output);
    }

    public void video2RGB(View view) {
        //视频转换成RGB格式，并在原生播放
        FFmpegUtils.video2RGB(input, videoView.getHolder().getSurface());
    }

    public void sound2PCM(View view) {
        //输出格式
        output = new File(Environment.getExternalStorageDirectory(), "decode.pcm").getAbsolutePath();
        //音频转换成PCM格式
        FFmpegUtils.sound2PCM(input, output);
    }

    public void soundPlay(View view) {
        FFmpegUtils utils = new FFmpegUtils();
        //音频转换成PCM，并在原生播放
        utils.soundPlay(input);
    }

    public void videoAndSoundPlay(View view) {

        if(!inputFile.exists()){
            Log.e("lsw",input);
            Toast.makeText(this,"文件不存在",Toast.LENGTH_SHORT).show();
            return;
        }

        FFmpegUtils utils = new FFmpegUtils();
        //音视频播放
        utils.videoAndSoundPlay(input, videoView.getHolder().getSurface());
    }
}
