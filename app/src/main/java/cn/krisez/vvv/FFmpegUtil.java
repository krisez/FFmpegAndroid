package cn.krisez.vvv;

/**
 * Created by zhouchaoxing on 2019/10/24
 */
public class FFmpegUtil {
    static {
        System.loadLibrary("avcodec");
        System.loadLibrary("avdevice");
        System.loadLibrary("avfilter");
        System.loadLibrary("avformat");
        System.loadLibrary("avutil");
        System.loadLibrary("swresample");
        System.loadLibrary("swscale");
        System.loadLibrary("native-lib");
    }
    public static native String stringFromJNI();
    public static native String exec(String[] cmd);

}
