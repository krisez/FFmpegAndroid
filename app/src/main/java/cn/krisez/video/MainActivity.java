package cn.krisez.video;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.os.Environment;
import android.widget.TextView;

import java.io.File;

import cn.krisez.video.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity {

    // Used to load the 'video' library on application startup.
    static {
        System.loadLibrary("iovffmpeg");
        System.loadLibrary("iovideocmd");
    }

    private ActivityMainBinding binding;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        // Example of a call to a native method
        TextView tv = binding.sampleText;
        tv.setText(stringFromJNI());
        File file = new File(getExternalCacheDir(),System.currentTimeMillis() + ".mp4");
        if(!file.getParentFile().exists()){
            file.mkdirs();
        }
        new Thread(() -> fff(new String[]{
                "ffmpeg","-d",
                "-accurate_seek",
                "-ss", "20",
                "-t", "15",
                "-i", "/storage/emulated/0/Movies/互传/video/pop-star-kda.mp4",
                "-codec", "copy", "-avoid_negative_ts", "1",
                Environment.getExternalStorageDirectory() + "/" + System.currentTimeMillis() + ".mp4"
        })).run();
    }

    /**
     * A native method that is implemented by the 'video' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
    public native int fff(String[] args);
}