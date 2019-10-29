package cn.krisez.vvv;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.View;
import android.widget.TextView;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Example of a call to a native method
        final TextView tv = findViewById(R.id.sample_text);
        tv.setText(FFmpegUtil.stringFromJNI());
        tv.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                tv.setText(FFmpegUtil.exec(new String[]{
                        "ffmpeg",
                        "-i",
                        Environment.getExternalStorageDirectory()+"/666.mp4",
                        "-i",
                        Environment.getExternalStorageDirectory()+"/aaa.mp3",
                        "-filter_complex",
                        "amix=inputs=2:duration=shortest",
                        Environment.getExternalStorageDirectory() + "/" + System.currentTimeMillis() + ".mp4"
                }));
            }
        });
    }
}
