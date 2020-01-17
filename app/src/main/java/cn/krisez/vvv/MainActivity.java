package cn.krisez.vvv;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.View;
import android.widget.TextView;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.OutputStream;

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
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        String out = Environment.getExternalStorageDirectory() + "/" + System.currentTimeMillis() + ".mp4";
                        tv.setText("a  "+FFmpegUtil.exec(new String[]{
                                "ffmpeg",
                                "-i",
                                Environment.getExternalStorageDirectory()+"/ysgs.mp4",
                                "-ss","14",
                                "-c","copy","-t","10",
                                out
                        }));
                    }
                }).run();
            }
        });
    }
}
