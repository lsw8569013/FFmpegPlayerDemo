package com.handsome.ndkffmpeg;

import android.content.Context;
import android.graphics.PixelFormat;
import android.util.AttributeSet;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * =====作者=====
 * 许英俊
 * =====时间=====
 * 2017/9/6.
 */

public class VideoView extends SurfaceView {
    public VideoView(Context context) {
        this(context, null);
    }

    public VideoView(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public VideoView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    public void init() {
        getHolder().setFormat(PixelFormat.RGBA_8888);
    }
}
