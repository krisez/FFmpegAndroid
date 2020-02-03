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
        System.loadLibrary("postproc");
        System.loadLibrary("native-ffmpeg");
//        System.loadLibrary("native-ffprobe");
    }
    public static native String stringFromJNI();
    public static native int ffmpegExec(String[] cmd);
    public static native int ffprobeExec(String[] cmd);

}
