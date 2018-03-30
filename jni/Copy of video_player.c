#include "com_example_ndk_ffmpeg_FFmpegUtils.h"

#include <stdlib.h>
#include <stdio.h>
//usleep需要
#include <unistd.h>
#include <android/log.h>
//Surface相关
#include <android/native_window_jni.h>
#include <android/native_window.h>

#include<pthread.h>

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
//音频重采样缓冲区大小
#define MAX_AUDIO_FRME_SIZE 48000 * 4

struct Player{
	//java虚拟机对象
	JavaVM *javaVM;

	//封装格式上下文
	AVFormatContext *avFormatCtx;
	//音频流视频流索引位置
	int video_stream_index;
	int audio_stream_index;
	//解码器上下文数组
	AVCodecContext *avCodecCtx[MAX_STREAM];
	//解码线程id
	pthread_t decode_threads[MAX_STREAM];
	//窗体绘制
	ANativeWindow* nativeWindow;

	SwrContext *swrCtx;
	//输入的采样格式
	enum AVSampleFormat in_sample_fmt;
	//输出采样格式16bit PCM
	enum AVSampleFormat out_sample_fmt;
	//输入采样率
	int in_sample_rate;
	//输出采样率
	int out_sample_rate;
	//输出的声道个数
	int out_channel_nb;

	//JNI
	jobject audio_track;
	jmethodID audio_track_write_mid;
};

/**
 * 初始化封装格式上下文，获取音频视频流的索引位置
 */
void init_input_format_ctx(struct Player *player,const char* input_cstr ){
	//1.注册所有组件
	av_register_all();

	//封装格式上下文，统领全局的结构体，保存了视频文件封装格式的相关信息
	AVFormatContext *avFormatCtx = avformat_alloc_context();

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
	player->avFormatCtx = avFormatCtx;
}

/**
 * 初始化解码器上下文
 *
 * stream_index:音频或者视频的index，获取对应的解码器
 */
void init_codec_context(struct Player *player,int stream_index){
	AVFormatContext *avFormatCtx = player->avFormatCtx;

	//从视频流或音频流中获取对应的解码器上下文
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

//子线程解码
void decode_data(void* arg){
	struct Player *player = (struct Player*)arg;
	AVFormatContext *avFormatCtx = player->avFormatCtx;

	//开辟缓冲区AVPacket用于存储一帧一帧的压缩数据（H264）
	AVPacket *avPacket =(AVPacket*)av_mallocz(sizeof(AVPacket));

	int frame_count = 0;

	//每次读取一帧,存入avPacket
	while(av_read_frame(avFormatCtx,avPacket)>=0){
		if(avPacket->stream_index == player->video_stream_index){
			//解码视频
			decode_video(player,avPacket);
			LOGI("video_frame_count:%d",frame_count++);
		}else if(avPacket->stream_index==player->audio_stream_index){
			//解码音频，这里曾经出现了一个小问题，我在测试解码音频的时候，将解码视频的子线程注释了，然而在这里这个位置
			//上边的decode_video方法没有注释，导致播放的时候，视频也同时进行了播放，并且由于音频和视频在同一个线程中解码，导致
			//音频和视频都有一定程度的卡顿现象，目前达到的效果只是分别开启线程播放视频或者单独播放音频，音视频同步尚未尝试
			//decode_audio(player,avPacket);
			LOGI("audio_frame_count:%d",frame_count++);
		}
		//读取完一次释放一次
		av_free_packet(avPacket);
	}
	LOGI("解码完成");

}

/**
 * 解码音频准备
 */
void decode_audio_prepare(struct Player *player){

	AVCodecContext *avCodecCtx = player->avCodecCtx[player->audio_stream_index];

	/**********  重采样设置参数---start   *****/

	//输入的采样格式
	enum AVSampleFormat in_sample_fmt = avCodecCtx->sample_fmt;
	//输出的采样格式16bit PCM
	enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
	//输入采样率
	int in_sample_rate = avCodecCtx->sample_rate;
	//输出的采样率设置为和输入相同
	int out_sample_rate = in_sample_rate;
	//输入的声道布局
	uint64_t in_ch_layout = avCodecCtx->channel_layout;
	//输出的声道布局(立体声，也可以设置为和输入相同)
	uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
	//输出的声道个数,从输出的声道布局中获取
	int out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);
	//frame->16bit 44100 PCM 统一音频采样格式与采样率
	SwrContext *swrCtx = swr_alloc();
	/**
	 * struct SwrContext *swr_alloc_set_opts(struct SwrContext *s,
     *        int64_t out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate,
     *        int64_t  in_ch_layout, enum AVSampleFormat  in_sample_fmt, int  in_sample_rate,
     *        int log_offset, void *log_ctx);
	 */
	swr_alloc_set_opts(swrCtx,out_ch_layout,out_sample_fmt,out_sample_rate,
			in_ch_layout,in_sample_fmt,in_sample_rate,0,NULL);
	swr_init(swrCtx);

	/**********  重采样设置参数---end   *****/

	//保存参数到结构体中
	player->in_sample_fmt = in_sample_fmt;
	player->out_sample_fmt = out_sample_fmt;
	player->in_sample_rate = in_sample_rate;
	player->out_sample_rate = out_sample_rate;
	player->out_channel_nb = out_channel_nb;
	player->swrCtx = swrCtx;
}

void jni_audio_prepare(JNIEnv *env,jobject jthiz,struct Player *player){
	//获取播放器工具类class
	jclass player_class = (*env)->GetObjectClass(env,jthiz);

	//获取类中方法createAudioTrack的id
	jmethodID create_audio_track_id = (*env)->GetMethodID(env,player_class,"createAudioTrack","(II)Landroid/media/AudioTrack;");

	//调用这个方法得到AudioTrack对象
	jobject audio_track = (*env)->CallObjectMethod(env,jthiz,create_audio_track_id,player->out_sample_rate,player->out_channel_nb);
	//调用AudioTrack.play方法
	jclass audio_track_class = (*env)->GetObjectClass(env,audio_track);

	//获取AudioTrack类中的play方法id
	jmethodID audio_track_play_mid = (*env)->GetMethodID(env,audio_track_class,"play","()V");
	(*env)->CallVoidMethod(env,audio_track,audio_track_play_mid);

	//AudioTrack.write
	jmethodID audio_track_write_id = (*env)->GetMethodID(env,audio_track_class,"write","([BII)I");

	//设置为全局引用，否则会报错,JNI ERROR(app bug):accessed stale local reference 0xxxxxxxx
	//在子线程中使用jobject或者jclass,要把相应的对象设置为全局引用，否则访问不到
	player->audio_track = (*env)->NewGlobalRef(env,audio_track);
	//(*env)->DeleteGlobalRef，注意全局引用使用完毕要delete
	player->audio_track_write_mid = audio_track_write_id;
}

/**
 * 解码音频
 */
void decode_audio(struct Player *player,AVPacket *avPacket){

	AVCodecContext *avFormatCtx = player->avCodecCtx[player->audio_stream_index];

	//开辟空间存储压缩数据
	AVFrame *avFrame = av_frame_alloc();
	//Zero if no frame could be decoded, otherwise it is non-zero
	int got_frame;
	/**
	 * int avcodec_decode_audio4(AVCodecContext *avctx, AVFrame *frame,
	 *      int *got_frame_ptr, const AVPacket *avpkt);
	 */
	avcodec_decode_audio4(avFormatCtx,avFrame,&got_frame,avPacket);
	//重采样缓冲区
	uint8_t *out_buffer = av_malloc(MAX_AUDIO_FRME_SIZE);
	//解码一帧成功
	if(got_frame){
		/**
		 * int swr_convert(struct SwrContext *s, uint8_t **out, int out_count,
                                const uint8_t **in , int in_count);
		 */
		swr_convert(player->swrCtx,&out_buffer,MAX_AUDIO_FRME_SIZE,(const uint8_t **)avFrame->data,avFrame->nb_samples);
		/**
		 * 获取sample的size
		 * int av_samples_get_buffer_size(int *linesize, int nb_channels, int nb_samples,
                               enum AVSampleFormat sample_fmt, int align);
		 */
		int out_buffer_size = av_samples_get_buffer_size(NULL,player->out_channel_nb,avFrame->nb_samples,player->out_sample_fmt,1);

		//关联当前线程的JNIEnv
		JavaVM *javaVM = player->javaVM;
		JNIEnv *env;
		(*javaVM)->AttachCurrentThread(javaVM,&env,NULL);

		//out_buffer缓冲区数据，转成byte数组,需要用到env,所以在上边获取
		//调用write方法需要传入一个byte数组，这个数组是包含out_buffer数组内容的，所以需要转换
		//创建一个和out_buffer数组同样大小的数组
		jbyteArray audio_sample_array = (*env)->NewByteArray(env,out_buffer_size);
		//获取可以操作这个数组的指针
		jbyte* sample_bytep = (*env)->GetByteArrayElements(env,audio_sample_array,NULL);
		//out_buffer的数据复制到sampe_bytep
		memcpy(sample_bytep,out_buffer,out_buffer_size);
		//同步
		(*env)->ReleaseByteArrayElements(env,audio_sample_array,sample_bytep,0);

		//AudioTrack.write PCM数据
		(*env)->CallIntMethod(env,player->audio_track,player->audio_track_write_mid,
				audio_sample_array,0,out_buffer_size);
		//TODO
		//操作完成一次就释放一次数组，否则会溢出
		(*env)->DeleteLocalRef(env,audio_sample_array);
		(*javaVM)->DetachCurrentThread(javaVM);
		usleep(1000 * 16);

	}
	av_frame_free(&avFrame);
}

/**
 * 解码视频准备
 */
void decode_video_prepare(JNIEnv *env,struct Player *player,jobject surface){
	//窗体  保存到结构体中
	player->nativeWindow = ANativeWindow_fromSurface(env,surface);
}
/**
 * 解码视频
 */
void decode_video(struct Player *player,AVPacket *avPacket){

	//开辟缓冲区AVFrame用于存储解码后的像素数据(YUV)
	AVFrame *yuv_Frame = av_frame_alloc();
	//开辟缓冲区AVFrame用于存储转成rgba8888后的像素数据(YUV)
	AVFrame *rgb_Frame = av_frame_alloc();
	//绘制时的缓冲区
	ANativeWindow_Buffer outBuffer;

	AVCodecContext *avCodecCtx = player->avCodecCtx[player->video_stream_index];

	//是否获取到视频像素数据的标记(Zero if no frame could be decompressed, otherwise, it is nonzero.)
	int got_picture;
	int decode_result;
	//筛选视频压缩数据（根据流的索引位置判断）
	if(avPacket->stream_index == player->video_stream_index){
		//7.解码一帧视频压缩数据，得到视频像素数据
		decode_result = avcodec_decode_video2(avCodecCtx,yuv_Frame,&got_picture,avPacket);

		if (decode_result < 0){
			LOGE("%s","解码错误");
			return;
		}
		//为0说明全部解码完成，非0正在解码
		if (got_picture){

			//设置缓冲区的属性    format 注意格式需要和surfaceview指定的像素格式相同
			ANativeWindow_setBuffersGeometry(player->nativeWindow, avCodecCtx->width, avCodecCtx->height, WINDOW_FORMAT_RGBA_8888);
			ANativeWindow_lock(player->nativeWindow,&outBuffer,NULL);

			//设置缓冲区像素格式,rgb_frame的缓冲区与outBuffer.bits时同一块内存
			avpicture_fill((AVPicture*)rgb_Frame,outBuffer.bits,PIX_FMT_RGBA,avCodecCtx->width,avCodecCtx->height);

			//按照yvu的顺序传参，如下，颜色正常，可以参照示例程序
			I420ToARGB(yuv_Frame->data[0],yuv_Frame->linesize[0],
					yuv_Frame->data[2],yuv_Frame->linesize[2],
					yuv_Frame->data[1],yuv_Frame->linesize[1],
					rgb_Frame->data[0],rgb_Frame->linesize[0],
					avCodecCtx->width,avCodecCtx->height);
			//unlock
			ANativeWindow_unlockAndPost(player->nativeWindow);

			//每次都需要sleep一下，否则会播放一帧之后就崩溃
			usleep(1000 * 16);
		}
	}
	av_frame_free(&yuv_Frame);
	av_frame_free(&rgb_Frame);
}

JNIEXPORT void JNICALL Java_com_example_ndk_1ffmpeg_FFmpegUtils_play
(JNIEnv *env, jobject jobj, jstring input_jstr, jobject surface){

		const char* input_cstr = (*env)->GetStringUTFChars(env,input_jstr,NULL);

		//给结构体申请一块空间(最后需要free掉)
		struct Player *player = (struct Player*)malloc(sizeof(struct Player));
		//获取Java虚拟机对象
		(*env)->GetJavaVM(env,&(player->javaVM));
		//初始化封装格式上下文
		init_input_format_ctx(player,input_cstr);

		int video_stream_index = player->video_stream_index;
		int audio_stream_index = player->audio_stream_index;
		//获取音视频解码器，并打开
		init_codec_context(player,video_stream_index);
		init_codec_context(player,audio_stream_index);

		decode_video_prepare(env,player,surface);
		decode_audio_prepare(player);

		jni_audio_prepare(env,jobj,player);
		//int pthread_create(pthread_t *thread, pthread_attr_t const * attr,
		//void *(*start_routine)(void *), void * arg);
		//创建子线程解码视频
		//pthread_create(&(player->decode_threads[video_stream_index]),NULL,decode_data,(void*)player);
		//创建子线程解码音频
		pthread_create(&(player->decode_threads[audio_stream_index]),NULL,decode_data,(void*)player);

}
