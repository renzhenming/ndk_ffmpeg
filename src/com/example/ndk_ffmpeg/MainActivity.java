package com.example.ndk_ffmpeg;

import java.io.File;

import android.app.Activity;
import android.os.Bundle;
import android.os.Environment;
import android.view.Surface;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Spinner;

public class MainActivity extends Activity {

	private VideoView videoView;
	private FFmpegUtils player;
	private Spinner sp_video;
	
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);
		videoView = (VideoView) findViewById(R.id.video_view);
		sp_video = (Spinner) findViewById(R.id.sp_video);
		String[] videoArray = getResources().getStringArray(R.array.video_list);
		ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, 
				android.R.layout.simple_list_item_1, 
				android.R.id.text1, videoArray);
		sp_video.setAdapter(adapter);
		player = new FFmpegUtils();
		
	}
	
	public void mPlay(View btn){
		String video = sp_video.getSelectedItem().toString();
		String input = new File(Environment.getExternalStorageDirectory(),video).getAbsolutePath();
		//String input = new File(Environment.getExternalStorageDirectory(),"input.mp4").getAbsolutePath();
		//Surface传入到Native函数中，用于绘制
		Surface surface = videoView.getHolder().getSurface();
		player.render(input, surface);
	}
	public void mSound(View btn){
		String input = new File(Environment.getExternalStorageDirectory(),"input.mp4").getAbsolutePath();
		String output = new File(Environment.getExternalStorageDirectory(),"music_sound.pcm").getAbsolutePath();
		player.sound(input, output);
	}
	public void mBothPlay(View btn){
		//String input = new File(Environment.getExternalStorageDirectory(),"input.mp4").getAbsolutePath();
		String video = sp_video.getSelectedItem().toString();
		String input = new File(Environment.getExternalStorageDirectory(),video).getAbsolutePath();
		Surface surface = videoView.getHolder().getSurface();
		player.play(input, surface);
	}

}
