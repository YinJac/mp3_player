#include <pthread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <alsa/asoundlib.h>
#include "data_fifo.h"
#include <stdlib.h>
#include <unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h> 
#include <stdbool.h>
#include "player.h"
#include <libcchip/platform.h>

fifo *recvqueue;
#define WAITTIME 20
#define ALSA_MAX_BUFFER_TIME 500000

#define BUF_SIZE 32*1024
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000
#define FRAME_BUFFER_SIZE 44100
#define MAX_AUDIO_FRAME_SIZE 22050
#define OUTPUT_CHANNEL 2

//将mp3文件打开并将文件数据写入到recvqueue中
void *convert_audiofile_to_fifo(void *argv)
{
	char buf[BUF_SIZE] = {0};
	int fd = -1;
	if(argv != NULL){
		fd = open(argv, O_RDONLY);
	}else{
		fd = open("test.mp3", O_RDONLY);
	}
	if(fd < 0){
		return NULL;
	}
	int ret = 0;
	unsigned int count = 0;
	while(1){
		memset(buf, 0, BUF_SIZE);
		lseek(fd, count, SEEK_SET);
		ret = read(fd, buf, BUF_SIZE);
		if(ret <= 0){
			err("=====================file read finish=======================\n");
			break;
		}
		while(ret > 0 ){
			if(!is_fifo_full(recvqueue)){
				fifo_put(recvqueue, buf, ret);
				count += ret;
				ret = 0;
			}else{
				err("=====================file read pause=======================\n");
				usleep(100*1000);
			}
		}
	}
	close(fd);
	return NULL;
}



 //注册av_read_frame的回调函数，这里只是最简处理，实际应用中应加上出错处理，超时等待...
int read_data(void *opaque, uint8_t *buf, int buf_size) {
	int ret = 0;
	printf("read data %d\n", buf_size);
	if(is_fifo_empty(recvqueue)){
		return ret;
	}else{
		ret = fifo_pop(recvqueue, buf, buf_size);
	}

	// printf("read data Ok %d\n", buf_size);
	return ret;
}

snd_pcm_t *m_pcmHandle;
bool m_abortFlag;
size_t m_chunkBytes;
size_t m_bits_per_frame;
#define OUTPUT_CHANNEL 2
#define OUTPUT_SAMPLE_RATE 44100

 bool openDevice() {
	 m_abortFlag = false;
	 int ret = snd_pcm_open(&m_pcmHandle, "tipsound", SND_PCM_STREAM_PLAYBACK, 0);
	 if (ret < 0) {
		 return false;
	 }
	 return true;
 }
 
 bool setParams(unsigned int rate, unsigned int channels) {
	 snd_pcm_hw_params_t *params = NULL;
	 uint32_t buffer_time = 0;
	 uint32_t period_time = 0;
	 int ret = 0;
	 snd_pcm_hw_params_alloca(&params);
 
	 ret = snd_pcm_hw_params_any(m_pcmHandle, params);
	 if (ret < 0) {
		 return false;
	 }
	 ret = snd_pcm_hw_params_set_access(m_pcmHandle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	 if (ret < 0) {
		 return false;
	 }
	 ret = snd_pcm_hw_params_set_format(m_pcmHandle, params, SND_PCM_FORMAT_S16_LE);
	 if (ret < 0) {
		 return false;
	 }
	 ret = snd_pcm_hw_params_set_channels(m_pcmHandle, params, channels);
	 if (ret < 0) {
		 return false;
	 }
	 ret = snd_pcm_hw_params_set_rate_near(m_pcmHandle, params, &rate, 0);
	 if (ret < 0) {
		 return false;
	 }
	 ret = snd_pcm_hw_params_get_buffer_time_max(params, &buffer_time, 0);
	 if (ret < 0) {
		 return false;
	 }
	 buffer_time = buffer_time > ALSA_MAX_BUFFER_TIME ? ALSA_MAX_BUFFER_TIME : buffer_time;
	 period_time = buffer_time / 4;
	 ret = snd_pcm_hw_params_set_buffer_time_near(m_pcmHandle, params, &buffer_time, 0);
	 if (ret < 0) {
		 return false;
	 }
	 ret = snd_pcm_hw_params_set_period_time_near(m_pcmHandle, params, &period_time, 0);
	 if (ret < 0) {
		 return false;
	 }
 
	 ret = snd_pcm_hw_params(m_pcmHandle, params);
	 if (ret < 0) {
		 return false;
	 }
	 int m_alsaCanPause = snd_pcm_hw_params_can_pause(params);
 
	 snd_pcm_uframes_t chunk_size = 0;
	 snd_pcm_uframes_t buffer_size = 0;
	 ret = snd_pcm_hw_params_get_period_size(params, &chunk_size, 0);
	 if (ret < 0) {
		 return false;
	 }
	 ret = snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	 if (ret < 0) {
		 return false;
	 }
	 int bits_per_sample = snd_pcm_format_physical_width(SND_PCM_FORMAT_S16_LE);
	 m_chunkBytes = chunk_size * bits_per_sample / 8;
	 m_bits_per_frame = bits_per_sample * channels;
	 myPlayInf("chunBytes = %d, chunk_size = %d, bits_per_frame=%d\n", m_chunkBytes, chunk_size, m_bits_per_frame );
	 return true;
 }




 void writeStream(unsigned int channels,
                                  const void *buffer,
                                  unsigned long buff_size) {
    if (m_pcmHandle == NULL) {
        return;
    }
	char *p = (char *)buffer;
	size_t chunk_size = m_chunkBytes * 8 / m_bits_per_frame;

	int r = 0;
	size_t c = buff_size * 8 / m_bits_per_frame;
	if (c < chunk_size) {
		snd_pcm_format_set_silence(SND_PCM_FORMAT_S16_LE,
			p + (c * m_bits_per_frame) / 8,
			(chunk_size - c) * channels);
		c = chunk_size;
	}
	while ( c > 0) {
		if (m_abortFlag) break;
		r = snd_pcm_writei(m_pcmHandle, p,  c);
		if (r == -EAGAIN || (r >= 0 && (size_t)r < c)) {
			if (m_abortFlag) break;
			snd_pcm_wait(m_pcmHandle, 100);
		} else if (r == -EPIPE) {
			if (m_abortFlag) break;
			snd_pcm_prepare(m_pcmHandle);
			if (m_abortFlag) break;
			r = snd_pcm_recover(m_pcmHandle, r, 1);
			myPlayWar("EPIPE, Buffer Underrun.");
			if ( (r > 0) && (r <= c) ) {
				if (m_abortFlag) break;
				r = snd_pcm_writei(m_pcmHandle, p, r );
			}
		} else if (r == -ESTRPIPE) {
			do{
				usleep(100);
				if (m_abortFlag) break;
				r = snd_pcm_resume(m_pcmHandle);
			}while(r == -EAGAIN);
			if (r < 0) {
				if (m_abortFlag) break;
				if ((r = snd_pcm_prepare(m_pcmHandle)) < 0) {
					myPlayWar("alsa resume: pcm_prepare %s", snd_strerror(r));
					break;
				}
				continue;
			}
		} else if (r < 0) {
			myPlayWar("failed to snd_pcm_writei: %s", snd_strerror(r));
			break;
		}
		if (r > 0) {
				c -= r;
				p += (r * m_bits_per_frame) / 8;
		}
    }
}

/*
功能说明：将MP3数据放入recvqueue数据缓存队列，avio_alloc_context通过read_data回调函数从recvqueue数据缓存队列中取出数据进行播放
*/
int main(int argc, char** argv) {

	if(-1 == init_data_fifo(&recvqueue, 1024*1024)){			//初始化数据缓存队列
		return -1;
	}

	pthread_t recv_thread;
	pthread_create(&recv_thread, NULL, convert_audiofile_to_fifo, argv[1]);		//测试：将mp3文件打开并将文件数据写入到recvqueue中
	pthread_detach(recv_thread);


	uint8_t *buf = av_mallocz(sizeof(uint8_t)*BUF_SIZE);

	av_register_all();

	AVCodec *pAudioCodec;
	AVCodecContext *pAudioCodecCtx = NULL;
	AVIOContext * pb = NULL;
	AVInputFormat *piFmt = NULL;
	AVFormatContext *pFmt = NULL;

	//step1:申请一个AVIOContext
	pb = avio_alloc_context(buf, BUF_SIZE, 0, NULL, read_data, NULL, NULL);		//read_data从recvqueue中读取数据到buf中
	if (!pb) {
		fprintf(stderr, "avio alloc failed!\n");
		return -1;
	}
	//step2:探测流格式
	if (av_probe_input_buffer(pb, &piFmt, "", NULL, 0, 0) < 0) {
		fprintf(stderr, "probe failed!\n");
		return -1;
	} else {
		fprintf(stdout, "probe success!\n");
		fprintf(stdout, "format: %s[%s]\n", piFmt->name, piFmt->long_name);
	}

	pFmt = avformat_alloc_context();
	pFmt->pb = pb; //step3:这一步很关键
	//step4:打开流
	if (avformat_open_input(&pFmt, "", piFmt, NULL) < 0) {
	fprintf(stderr, "avformat open failed.\n");
	return -1;
	} else {
	fprintf(stdout, "open stream success!\n");
	}
	//以下就和文件处理一致了
	if (avformat_find_stream_info(pFmt, NULL) < 0) {
	fprintf(stderr, "could not fine stream.\n");
	return -1;
	}

	av_dump_format(pFmt, 0, "", 0);

	int videoindex = -1;
	int audioindex = -1;
	int i;
    for (i = 0; i < pFmt->nb_streams; i++) {
		if (pFmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioindex = i;
            break;
		}
    }
	if ( audioindex < 0) {
		fprintf(stderr, " audioindex=%d\n", audioindex);
		return -1;
	}

	AVStream *pAst;
	pAst = pFmt->streams[audioindex];

    AVCodecParameters *codec_par = pAst->codecpar;
    AVCodec *codec = avcodec_find_decoder(codec_par->codec_id);
    pAudioCodecCtx = avcodec_alloc_context3(codec);


	pAudioCodec = avcodec_find_decoder(pAudioCodecCtx->codec_id);
    if (avcodec_parameters_to_context(pAudioCodecCtx, codec_par) < 0) {
        avformat_close_input(&pAudioCodecCtx);
        pAudioCodecCtx = NULL;
    }
	if (!pAudioCodec) {
		fprintf(stderr, "could not find audio decoder!\n");
		return -1;
	}
	if (avcodec_open2(pAudioCodecCtx, pAudioCodec, NULL) < 0) {
		fprintf(stderr, "could not open audio codec!\n");
		return -1;
	}

	uint8_t samples[AVCODEC_MAX_AUDIO_FRAME_SIZE*3/2];
	AVFrame *pframe = av_frame_alloc();
	AVPacket *pkt = av_packet_alloc();


    uint64_t out_channel_layout = AV_CH_LAYOUT_MONO;
	unsigned int m_outputChannel = 2;
    if (m_outputChannel == 1) {
        out_channel_layout = AV_CH_LAYOUT_MONO;
    } else if (m_outputChannel == 2) {
        out_channel_layout = AV_CH_LAYOUT_STEREO;
    }
	
	const enum AVSampleFormat c_outputFmt = AV_SAMPLE_FMT_S16;

    unsigned int m_bytesPersample = av_get_bytes_per_sample(c_outputFmt);
	unsigned int m_outputSamplerate = 44100;

    int64_t in_channel_layout = av_get_default_channel_layout(pAudioCodecCtx->channels);

    struct SwrContext *m_convertCtx = swr_alloc();
    m_convertCtx = swr_alloc_set_opts(m_convertCtx,
                                      out_channel_layout,
                                      c_outputFmt,
                                      m_outputSamplerate,
                                      in_channel_layout,
                                      pAudioCodecCtx->sample_fmt,
                                      pAudioCodecCtx->sample_rate,
                                      0,
                                      NULL);
    swr_init(m_convertCtx);


	
    uint8_t *frameBuffer = (uint8_t *)av_malloc(FRAME_BUFFER_SIZE);
    uint8_t *m_pcmBuffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE);

	fifo *frameFifo = NULL;
	init_data_fifo(&frameFifo, MAX_AUDIO_FRAME_SIZE);

	openDevice();
	setParams(OUTPUT_SAMPLE_RATE, OUTPUT_CHANNEL);
	int pos = 0;
	unsigned int len = 0;

	while(1) {
		if (av_read_frame(pFmt, pkt) >= 0){				
			if(pkt->stream_index != audioindex){
				av_packet_free(&pkt);
				continue;
			}
			avcodec_send_packet(pAudioCodecCtx, pkt);
			int error_code = avcodec_receive_frame(pAudioCodecCtx, pframe);
			if (error_code == 0) {
				memset(frameBuffer, 0, FRAME_BUFFER_SIZE);
				int frame_size = swr_convert(m_convertCtx,
											 &frameBuffer,
											 FRAME_BUFFER_SIZE,
											 (const uint8_t **) pframe->data,
											 pframe->nb_samples);
				len = frame_size * m_outputChannel * m_bytesPersample;
				av_frame_unref(pframe);
				av_packet_unref(pkt);
			} else {
				err("================================================\n");
				av_frame_unref(pframe);
				av_packet_unref(pkt);
				myPlayPerr(0,"av_read_frame");
				break;
			}
			
			uint8_t *p = m_pcmBuffer + pos;
			if (pos + len > MAX_AUDIO_FRAME_SIZE) {
				writeStream(OUTPUT_CHANNEL, m_pcmBuffer, pos);
				pos = 0;
				p = m_pcmBuffer;
				memcpy(p, frameBuffer, len);
				pos += len;
			} else {
				memcpy(p, frameBuffer, len);
				pos += len;
			}
		} else{
			err("================================================\n");
			break;
		}

	}
	av_packet_free(&pkt);

	av_free(buf);
	av_free(pframe);
	fifo_free(recvqueue);
	return 0;
	}
