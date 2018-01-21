package com.example.ndk_ffmpeg;

import android.R.integer;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.view.Surface;


/**
 * 
 * 视频播放的控制器
 */
public class FFmpegUtils {

	public native void render(String input,Surface surface);
	
	public native void sound(String input,String output);
	
	public AudioTrack createAudioTrack(){
		//固定格式的音频码流
		int sampleRateInHz = 44100;
		
		//声道布局
		int channelConfig = AudioFormat.CHANNEL_OUT_STEREO;//立体声
		
		int audioFormat = AudioFormat.ENCODING_PCM_16BIT;
		
		int bufferSizeInBytes = AudioTrack.getMinBufferSize(sampleRateInHz, channelConfig, audioFormat);
 		@SuppressWarnings("deprecation")
		AudioTrack audioTrack = new AudioTrack(AudioManager.STREAM_MUSIC,
				sampleRateInHz, channelConfig, audioFormat, bufferSizeInBytes, AudioTrack.MODE_STREAM);
 		//播放
 		//audioTrack.play();
 		//写入PCM
 		//audioTrack.write(audioData, offsetInBytes, sizeInBytes);
 		return audioTrack;
	}
	
	static{
		System.loadLibrary("avutil-54");
		System.loadLibrary("swresample-1");
		System.loadLibrary("avcodec-56");
		System.loadLibrary("avformat-56");
		System.loadLibrary("swscale-3");
		System.loadLibrary("postproc-53");
		System.loadLibrary("avfilter-5");
		System.loadLibrary("avdevice-56");
		System.loadLibrary("myffmpeg");
	}
}
