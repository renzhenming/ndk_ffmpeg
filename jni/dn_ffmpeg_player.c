#include "com_example_ndk_ffmpeg_FFmpegUtils.h"
#include <stdlib.h>
#include <stdio.h>
//usleep需要
#include <unistd.h>
#include <android/log.h>
//Surface相关
#include <android/native_window_jni.h>
#include <android/native_window.h>

//被引入的libyuv中有C++代码编写，在这里需要设置为用C来编译

#include "include/libyuv/libyuv.h"


#define LOGI(FORMAT,...) __android_log_print(ANDROID_LOG_INFO,"renzhenming",FORMAT,##__VA_ARGS__);
#define LOGE(FORMAT,...) __android_log_print(ANDROID_LOG_ERROR,"renzhenming",FORMAT,##__VA_ARGS__);

#define MAX_AUDIO_FRME_SIZE 48000 * 4

//封装格式
#include "include/ffmpeg/libavformat/avformat.h"
//解码
#include "include/ffmpeg/libavcodec/avcodec.h"
//缩放
#include "include/ffmpeg/libswscale/swscale.h"
//重采样
#include "libswresample/swresample.h"

/**
 * 音频播放
 */
JNIEXPORT void JNICALL Java_com_example_ndk_1ffmpeg_FFmpegUtils_sound
(JNIEnv *env, jobject jthiz, jstring input_jstr, jstring output_jstr){
	const char* input_cstr = (*env)->GetStringUTFChars(env,input_jstr,NULL);
	const char* output_cstr = (*env)->GetStringUTFChars(env,output_jstr,NULL);
	LOGI("%s","sound");

	//1.注册所有组件
	av_register_all();

	//2.获取 上下文对象
	AVFormatContext *avFormatCtx = avformat_alloc_context();

	//3.打开音频文件
	if(avformat_open_input(&avFormatCtx,input_cstr,NULL,NULL)){
		LOGI("%s","无法打开音频文件");
		return;
	}

	//4.获取音频文件信息
	if(avformat_find_stream_info(avFormatCtx,NULL)<0){
		LOGI("%s","无法获取音频文件信息");
		return;
	}

	//5.搜索音频流索引位置
	int i =0,audio_index=-1;
	for(;i<avFormatCtx->nb_streams;i++){
		if(avFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO){
			audio_index = i;
			break;
		}
	}

	//6.获取解码器
	AVCodecContext *avCodecCtx = avFormatCtx->streams[audio_index]->codec;
	AVCodec *avCodec = avcodec_find_decoder(avCodecCtx->codec_id);
	if(avCodec == NULL){
		LOGI("%s","无法获取解码器");
		return;
	}
	if(avcodec_open2(avCodecCtx,avCodec,NULL)<0){
		LOGI("%s","无法打开解码器");
		return;
	}

	//7.重采样设置参数-------------start

	//frame->16bit 44100 PCM 统一音频采样格式与采样率
	SwrContext *swrCtx = swr_alloc();
	//输出的声道布局（立体声）
	uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
	//输出采样格式16bit PCM
	enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
	//输出采样率
	int out_sample_rate = 44100;
	//获取输入的声道布局
	uint64_t in_ch_layout = avCodecCtx->channel_layout;
	//获取输入的采样格式
	enum AVSampleFormat in_sample_fmt = avCodecCtx->sample_fmt;
	//获取输入的采样率
	int in_sample_rate = avCodecCtx->sample_rate;

	/*
	 * struct SwrContext *s,
     * int64_t out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate,
     * int64_t  in_ch_layout, enum AVSampleFormat  in_sample_fmt, int  in_sample_rate,
     * int log_offset, void *log_ctx);
 	 *
	 * @param s               existing Swr context if available, or NULL if not
	 * @param out_ch_layout   output channel layout (AV_CH_LAYOUT_*)
	 * @param out_sample_fmt  output sample format (AV_SAMPLE_FMT_*).
	 * @param out_sample_rate output sample rate (frequency in Hz)
	 * @param in_ch_layout    input channel layout (AV_CH_LAYOUT_*)
	 * @param in_sample_fmt   input sample format (AV_SAMPLE_FMT_*).
	 * @param in_sample_rate  input sample rate (frequency in Hz)
	 * @param log_offset      logging level offset
	 * @param log_ctx         parent logging context, can be NULL*/

	 swr_alloc_set_opts(swrCtx, out_ch_layout,out_sample_fmt,out_sample_rate,
			  in_ch_layout,in_sample_fmt,in_sample_rate,
			  0, NULL);
	 swr_init(swrCtx);

	 int got_frame = 0,index = 0, result;
	 //开辟空间存储压缩数据
	 AVPacket *avPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
	 //开辟空间存储解压缩数据
	 AVFrame *avFrame = av_frame_alloc();
	 //16bit 44100 PCM 数据
	 uint8_t *out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRME_SIZE);
	 //写入文件
	 FILE *fp_pcm = fopen(output_cstr,"wb");
	 //输出的声道个数
	 int out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);

	 //------------------JNI begin 调用Java代码-------------------------

	 //1.调用FFmpegUtils中的createAudioTrack方法获取AudioTrack对象
	 jclass ffmpeg_class = (*env)->GetObjectClass(env,jthiz);
	 jmethodID create_audiotrack_mid = (*env)->GetMethodID(env,ffmpeg_class,"createAudioTrack","()Landroid/media/AudioTrack;");
	 jobject audio_track = (*env)->CallObjectMethod(env,jthiz,create_audiotrack_mid);
	 //调用AudioTack play方法
	 jclass audio_track_class = (*env)->GetObjectClass(env,audio_track);
	 jmethodID audiotrack_play_mid = (*env)->GetMethodID(env,audio_track_class,"play","()V");
	 (*env)->CallVoidMethod(env,audio_track,audiotrack_play_mid);
	 //解码每一帧成功后需要调用audiotrack的write方法，所以获取到这个方法的mid
	 jmethodID audiotrack_write_mid = (*env)->GetMethodID(env,audio_track_class,"write","([BII)I");

	 //-------------------      JNI end       ------------------------

	 while(av_read_frame(avFormatCtx,avPacket)>= 0){
		 //解码音频数据
		 result = avcodec_decode_audio4(avCodecCtx,avFrame,&got_frame,avPacket);
		 if(result < 0){
		 	LOGI("%s","解码完成");
		 }

		 //解码一帧成功
		 if(got_frame>0){
			 LOGI("解码：%d",index++);
			 swr_convert(swrCtx,&out_buffer,MAX_AUDIO_FRME_SIZE,avFrame->data,avFrame->nb_samples);
			 //获取sample的size
			 int out_buffer_size = av_samples_get_buffer_size(NULL, out_channel_nb,
						avFrame->nb_samples, out_sample_fmt, 1);
			 //fwrite(out_buffer,1,out_buffer_size,fp_pcm);
			 //AudioTrack writePCM数据，通过调用Java代码获取到AudioTrack

			 //调用write方法需要传入一个byte数组，这个数组是包含out_buffer数组内容的，所以需要转换
			 //创建一个和out_buffer数组同样大小的数组
			 jbyteArray audio_sample_array = (*env)->NewByteArray(env,out_buffer_size);
			 //获取可以操作这个数组的指针
			 jbyte* sample_bytep = (*env)->GetByteArrayElements(env,audio_sample_array,NULL);
			 //将out_buffer的数据复制到sampe_bytep
			 memcpy(sample_bytep,out_buffer,out_buffer_size);
			 //同步数据
			 (*env)->ReleaseByteArrayElements(env,audio_sample_array,sample_bytep,0);

			 (*env)->CallIntMethod(env,audio_track,audiotrack_write_mid,
					 audio_sample_array,0,out_buffer_size);

			 //操作完成一次就释放一次数组，否则会溢出
			 (*env)->DeleteLocalRef(env,audio_sample_array);
			 usleep(1000 * 16);
		 }

		 av_free_packet(avPacket);
	 }
	 fclose(fp_pcm);
	 av_frame_free(avFrame);
	 av_freep(out_buffer);
	 swr_free(&swrCtx);
	 avcodec_close(avCodecCtx);
	 avformat_close_input(&avFormatCtx);
	 (*env)->ReleaseStringUTFChars(env,input_jstr,input_cstr);
	 (*env)->ReleaseStringUTFChars(env,output_jstr,output_cstr);
}

/**
 * 视频播放
 */
JNIEXPORT void JNICALL Java_com_example_ndk_1ffmpeg_FFmpegUtils_render
(JNIEnv *env, jobject jobj, jstring input_jstr, jobject surface){
		const char* input_cstr = (*env)->GetStringUTFChars(env,input_jstr,NULL);

		//1.注册所有组件
		av_register_all();

		//封装格式上下文，统领全局的结构体，保存了视频文件封装格式的相关信息
		AVFormatContext *avFormatCtx = avformat_alloc_context();

		//2.打开输入视频文件
		if(avformat_open_input(&avFormatCtx,input_cstr,NULL,NULL)!=0){
			LOGE("%s","无法打开输入视频文件");
			return;
		}

		//3.获取视频文件信息
		if(avformat_find_stream_info(avFormatCtx,NULL)<0){
			LOGE("%s","无法获取视频文件信息");
			return;
		}

		//4.获取对应视频的解码器
		//一个视频包含一系列信息的组合，比如视频流音频流字幕等等，avformat_find_stream_info读取视频
		//信息之后，会将这些所以信息分组保存起来，也就是相当于一个数组，数组的每一个位置保存一个对应的信息，在
		//AVFormatContext 中是有这样一个数组的(avFormatCtx->streams),这个数组的每个index中都保存有
		//一类信息，而我们需要对视频进行解码，所以我们首先要找到视频信息在这个数组中的index，然后就可以获取到
		//这个index上的视频，然后就可以获取到这个视频的编码方式，然后获取解码器

		//a)获取到视频流的index
		int i = 0;
		int video_index = -1;
		for(;i<avFormatCtx->nb_streams;i++){  //avFormatCtx->nb_streams  number of streams
			//判断流的类型是不是 AVMEDIA_TYPE_VIDEO
			if(avFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
				video_index = i;
				break;
			}
		}

		if (video_index == -1){
			LOGE("%s","找不到视频流\n");
			return;
		}

		//b)从视频流中获取视频编解码器上下文
		AVCodecContext *avCodecCtx = avFormatCtx->streams[video_index]->codec;
		//c)根据编解码器id信息查找对应的编解码器
		AVCodec *avCodec = avcodec_find_decoder(avCodecCtx->codec_id);

		if (avCodec == NULL){
			LOGE("%s","找不到解码器\n");
			return;
		}

		//5.打开解码器
		if (avcodec_open2(avCodecCtx,avCodec,NULL)<0){
			LOGE("%s","解码器无法打开\n");
			return;
		}

		//输出视频信息
		LOGI("视频的文件格式：%s",avFormatCtx->iformat->name);
		//LOGI("视频时长：%l", (avFormatCtx->duration)/1000000);
		LOGI("视频的宽高：%d,%d",avCodecCtx->width,avCodecCtx->height);
		LOGI("解码器的名称：%s",avCodec->name);

		//6.读取输入文件数据
		//开辟缓冲区AVPacket用于存储一帧一帧的压缩数据（H264）
		AVPacket *avPacket =(AVPacket*)av_mallocz(sizeof(AVPacket));

		//开辟缓冲区AVFrame用于存储解码后的像素数据(YUV)
		AVFrame *yuv_Frame = av_frame_alloc();

		//开辟缓冲区AVFrame用于存储转成rgba8888后的像素数据(YUV)
		AVFrame *rgb_Frame = av_frame_alloc();


		//native绘制

		//窗体
		ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env,surface);
		//绘制时的缓冲区
		ANativeWindow_Buffer outBuffer;


		int frame_count = 0;
		//是否获取到视频像素数据的标记(Zero if no frame could be decompressed, otherwise, it is nonzero.)
		int got_picture;
		int decode_result;
		//每次读取一帧,存入avPacket
		while(av_read_frame(avFormatCtx,avPacket)>=0){
			//筛选视频压缩数据（根据流的索引位置判断）

			//TODO 这个判断是否需要
			if(avPacket->stream_index == video_index){
				//7.解码一帧视频压缩数据，得到视频像素数据
				decode_result = avcodec_decode_video2(avCodecCtx,yuv_Frame,&got_picture,avPacket);

				if (decode_result < 0){
					LOGE("%s","解码错误");
					return;
				}
				//为0说明全部解码完成，非0正在解码
				if (got_picture){
					LOGI("解码第%d帧",frame_count);
					//lock

					//设置缓冲区的属性    format 注意格式需要和surfaceview指定的像素格式相同
					ANativeWindow_setBuffersGeometry(nativeWindow, avCodecCtx->width, avCodecCtx->height, WINDOW_FORMAT_RGBA_8888);
					ANativeWindow_lock(nativeWindow,&outBuffer,NULL);
					//fix buffer
					//YUV-RGBA8888

					//设置缓冲区像素格式,rgb_frame的缓冲区与outBuffer.bits时同一块内存
					avpicture_fill((AVPicture*)rgb_Frame,outBuffer.bits,PIX_FMT_RGBA,avCodecCtx->width,avCodecCtx->height);
					/**
					 * int I420ToARGB(const uint8* src_y, int src_stride_y,
               	   	   const uint8* src_u, int src_stride_u,
               	   	   const uint8* src_v, int src_stride_v,
               	   	   uint8* dst_argb, int dst_stride_argb,
               	   	   int width, int height);
					 */

					//TODO 参数的顺序 导致的问题
					//data[0] ：y data[1] ：u  data[2] ：v
					//按照yuv的顺序传参，如下，会导致颜色不正常，偏绿色
					/*I420ToARGB(yuv_Frame->data[0],yuv_Frame->linesize[0],
							yuv_Frame->data[1],yuv_Frame->linesize[1],
							yuv_Frame->data[2],yuv_Frame->linesize[2],
							rgb_Frame->data[0],rgb_Frame->linesize[0],
							avCodecCtx->width,avCodecCtx->height);*/

					//按照yvu的顺序传参，如下，颜色正常，可以参照示例程序
					I420ToARGB(yuv_Frame->data[0],yuv_Frame->linesize[0],
							yuv_Frame->data[2],yuv_Frame->linesize[2],
							yuv_Frame->data[1],yuv_Frame->linesize[1],
							rgb_Frame->data[0],rgb_Frame->linesize[0],
							avCodecCtx->width,avCodecCtx->height);
					//unlock
					ANativeWindow_unlockAndPost(nativeWindow);

					//每次都需要sleep一下，否则会播放一帧之后就崩溃
					usleep(1000 * 16);
				}
			}
			//读取完一次释放一次
			av_free_packet(avPacket);
		}
		LOGI("解码完成");

		ANativeWindow_release(nativeWindow);

		(*env)->ReleaseStringUTFChars(env,input_jstr,input_cstr);

		av_frame_free(&yuv_Frame);

		avcodec_close(avCodecCtx);

		avformat_free_context(avFormatCtx);
}
