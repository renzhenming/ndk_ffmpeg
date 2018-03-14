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

//nb_streams，视频文件中存在，音频流，视频流，字幕
#define MAX_STREAM 2

struct Player{
	//封装格式上下文
	AVFormatContext *avFormatCtx;
	//音频流视频流索引位置
	int video_stream_index;
	int audio_stream_index;

	//解码器上下文数组
	AVCodecContext *avCodecCtx[MAX_STREAM];
};

/**
 * 初始化封装格式上下文，获取音频视频流的索引位置
 */
void init_input_format_ctx(struct Player *player,const char* input_cstr ){
	//1.注册所有组件
	av_register_all();

	//封装格式上下文，统领全局的结构体，保存了视频文件封装格式的相关信息
	AVFormatContext *avFormatCtx = favformat_alloc_context();

	//2.打开输入视频文件(AVFormatContext **ps, const char *url, AVInputFormat *fmt, AVDictionary **options)
	if(avformat_open_input(&avFormatCtx,input_cstr,NULL,NULL)!=0){
		LOGE("%s","无法打开输入视频文件");
		return;
	}

	//3.获取视频信息(AVFormatContext *ic, AVDictionary **options)
	if(avformat_find_stream_info(avFormatCtx,NULL)<0){
		LOGE("%s","无法获取视频文件信息");
		return;
	}
	//4.获取音频和视频流的索引位置
	int i;
	for(i = 0; i <avFormatCtx->nb_streams; i++){
		//视频
		if(avFormatCtx->streams[i]->codec->codec_type ==AVMEDIA_TYPE_VIDEO ){
			player->video_stream_index = i;
		}
		//音频
		if(avFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO){
			player->audio_stream_index = i;
		}
	}
	//5.将得到的上下文保存在结构体中
	//player->avFormatCtx = avFormatCtx;
}

/**
 * 初始化解码器上下文
 *
 * stream_index:音频或者视频的index，获取对应的解码器
 */
void init_codec_context(struct Player *player,int stream_index){
	AVFormatContext *avFormatCtx = player.avFormatCtx;

	//从视频流中获取视频编解码器上下文
	AVCodecContext *avCodecCtx = avFormatCtx->streams[stream_index]->codec;
	//根据编解码器id信息查找对应的解码器
	AVCodec *avCodec = avcodec_find_decoder(avCodecCtx->codec_id);

	if (avCodec == NULL){
		LOGE("%s","找不到解码器\n");
		return;
	}

	//打开解码器
	if (avcodec_open2(avCodecCtx,avCodec,NULL)<0){
		LOGE("%s","解码器无法打开\n");
		return;
	}

	//将获取到的解码器上下文保存在结构体中，这个解码器包括音频和视频
	player->avCodecCtx[stream_index] = avCodecCtx;
}

JNIEXPORT void JNICALL Java_com_example_ndk_1ffmpeg_FFmpegUtils_render
(JNIEnv *env, jobject jobj, jstring input_jstr, jobject surface){
		const char* input_cstr = (*env)->GetStringUTFChars(env,input_jstr,NULL);

		//给结构体申请一块空间(最后需要free掉)
		struct Player *player = (struct Player)malloc(sizeof(struct Player));
		//初始化封装格式上下文
		init_input_format_ctx(player,input_cstr);

		int video_stream_index = player->video_stream_index;
		int audio_stream_index = player->audio_stream_index;
		//获取音视频解码器，并打开
		init_codec_context(player,video_stream_index);
		init_codec_context(player,audio_stream_index);

		//TODO 2018/3/14

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


			free(player);
}
