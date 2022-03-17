#include "mp3dec.h"
#include "../wave_out.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define _OUTPUT_TO_FILE_ 0
#define _DEBUG_ 0

typedef struct{
	int samprate;
	int bitsPerSample;
	int nChans;
}MP3_TYPE;

#define READBUF_SIZE 3000
#define MP3BUFFER_SIZE 2304                        //数据帧一帧解码完最大2304*2字节
char readBuf[READBUF_SIZE];
short output[MP3BUFFER_SIZE];
char* mp3Path = "D:\\CloudMusic\\简单爱.mp3";      //系统盘(C:)下无法打开，所以放在D盘
char* pcmPath = "C:\\Users\\xxx\\Desktop\\2.pcm";

int main(int argc, const char *argv[])
{
	HMP3Decoder hMP3Decoder;         // mp3解码器指针
	MP3FrameInfo mp3FrameInfo;       // mp3帧信息
	int frames = 0;                  // 记录数据帧个数
	int bytesLeft = 0;               // 从mp3文件读入缓冲区中的剩余字节数
	int offset = 0;                  // 读偏移指针
	int err = 0, outputSamps = 0;
	char* readPtr = readBuf;
	FILE* mp3File;
	FILE* pcmFile;
	MP3_TYPE mp3player = {
		.bitsPerSample = 16,
		.nChans = 2,
		.samprate = 44100
	};

	/* 初始化MP3解码器 */
	hMP3Decoder = MP3InitDecoder();
	if (hMP3Decoder == 0) {
		printf("初始化helix解码库设备\n");
		return -1;
	}
	printf("初始化helix解码库设备完成！！！\n");

	/* 打开MP3文件 */
	mp3File = fopen(mp3Path, "rb");
	if (mp3File == NULL) {    //判断文件是否正常打开
		printf("fail to open %s: %s\n", mp3Path, strerror(errno));
		return -1;
	}
	

#if _OUTPUT_TO_FILE_
	/* 创建or打开PCM文件 */
	pcmFile = fopen(pcmPath, "wb");
	if (pcmFile == NULL) {   //判断文件是否正常打开
		printf("fail to open %s: %s\n", pcmPath, strerror(errno));
		fclose(mp3File);
		return;
	}
#endif

	/* 设置声卡参数 */
	//44100到时候需要根据歌曲的实际采样率来填写，16位深度，双声道
	Set_WIN_Params(NULL, mp3player.samprate, mp3player.bitsPerSample, mp3player.nChans);

	/* 读取MP3文件 */
	int br = fread(readBuf, 1, READBUF_SIZE, mp3File);
	if ((br <= 0)) {  // 读取失败
		printf("read <%s> fail\r\n", mp3Path);
		MP3FreeDecoder(hMP3Decoder);
		return -1;
	}

	bytesLeft += br;

	while (1) {
		/* find start of next mp3 frame - assume EOF if no sync found */
		offset = MP3FindSyncWord(readPtr, bytesLeft);
		if (offset < 0) {  // 没找到数据帧（一般都能找到，毕竟ID3V2数据不多）
#if _DEBUG_
			printf("【没找到数据帧】readPtr = 0x%p, bytesLeft = %d\n", readPtr, bytesLeft);
#endif
			br = fread(readBuf, 1, READBUF_SIZE, mp3File);
			if ((br <= 0)) {  // 读取失败
				printf("read <%s> fail\r\n", mp3Path);
				break;
			}
			readPtr = readBuf;
			bytesLeft = br;
			continue;
		}
		else { //找到数据帧
#if _DEBUG_
			printf("【找到数据帧】readPtr = 0x%p, bytesLeft = %d, offset = %d\n", readPtr, bytesLeft, offset);
#endif
			readPtr += offset;     // 偏移至同步字的位置
			bytesLeft -= offset;   // 同步字之后的数据大小（可能含多个数据帧或不足一个数据帧）

			/* 补充数据帧 */
			if (bytesLeft < 3000) {// 【疑问?】1024也是能正常播放的，但是NodeMCU32S上不行
				memmove(readBuf, readPtr, bytesLeft);
				br = fread(readBuf + bytesLeft, 1, READBUF_SIZE - bytesLeft, mp3File);
				if ((br <= 0)) break;

				readPtr = readBuf;
				bytesLeft += br;
#if _DEBUG_
				printf("【补充数据完成】readPtr = 0x%p, bytesLeft = %d\n", readPtr, bytesLeft);
#endif
			}

			int errs = MP3Decode(hMP3Decoder, &readPtr, &bytesLeft, output, 0);//调用一次解码一帧数据帧
			frames++;

			if (err != ERR_MP3_NONE) {   // 解码错误
				switch (err) {
					case ERR_MP3_INDATA_UNDERFLOW:
						printf("ERR_MP3_INDATA_UNDERFLOW\r\n");
						br = fread(readBuf, 1, READBUF_SIZE, mp3File); //重新读入缓冲区
						if ((br <= 0)) break;
						readPtr = readBuf;
						bytesLeft = br;
					case ERR_MP3_MAINDATA_UNDERFLOW:
						printf("ERR_MP3_MAINDATA_UNDERFLOW\r\n");
						break;
					default:
						printf("UNKNOWN ERROR:%d\r\n", err);
						// 跳过此帧
						if (bytesLeft > 0)
						{
							bytesLeft--;
							readPtr++;
						}
						break;
				}
			}
			else { // 解码正常
#if _DEBUG_
				printf("【解码正常】frames = %d, readPtr = %p, bytesLeft = %d\n", frames, readPtr, bytesLeft);
#endif
				MP3GetLastFrameInfo(hMP3Decoder, &mp3FrameInfo);  // 获取解码信息
				outputSamps = mp3FrameInfo.outputSamps;           // PCM数据个数
				if (outputSamps > 0) {
					if (1 == mp3FrameInfo.nChans) {
						//单声道数据需要复制一份到另一个声道
						int i;
						for (i = outputSamps - 1; i >= 0; i--)
						{
							output[i * 2] = output[i];
							output[i * 2 + 1] = output[i];
						}
						outputSamps *= 2;
					}
				}
				

#if _DEBUG_
				printf(" \r\n Bitrate       %dKbps", mp3FrameInfo.bitrate / 1000);
				printf(" \r\n Samprate      %dHz", mp3FrameInfo.samprate);
				printf(" \r\n BitsPerSample %db", mp3FrameInfo.bitsPerSample);
				printf(" \r\n nChans        %d", mp3FrameInfo.nChans);
				printf(" \r\n Layer         %d", mp3FrameInfo.layer);
				printf(" \r\n Version       %d", mp3FrameInfo.version);
				printf(" \r\n OutputSamps   %d", mp3FrameInfo.outputSamps);
				printf(" \r\n 按KEY1切换为耳机输出");
				printf(" \r\n 按KEY2切换为扬声器输出");
				printf("\r\n");
#endif
				if ((mp3player.samprate != mp3FrameInfo.samprate) && (mp3player.bitsPerSample != mp3FrameInfo.bitsPerSample) && (mp3player.nChans != mp3FrameInfo.nChans)) {
					mp3player.samprate = mp3FrameInfo.samprate;
					mp3player.bitsPerSample = mp3FrameInfo.bitsPerSample;
					mp3player.nChans = mp3FrameInfo.nChans;
					Set_WIN_Params(NULL, mp3player.samprate, mp3player.bitsPerSample, mp3player.nChans);
				}
				
				WIN_Play_Samples(output, sizeof(short)*outputSamps);
#if _OUTPUT_TO_FILE_
				fwrite((char*)output, sizeof(short), outputSamps, pcmFile);
#endif
			}
		}
		
	}

	if (fseek(mp3File, 0, SEEK_END) == 0) {
		printf("frames: %d\n", frames);
		printf("MP3解码完成\n");
	}
	else {
		printf("MP3解码存在异常\n");
	}
	
	MP3FreeDecoder(hMP3Decoder);
	WIN_Audio_close(); //关闭设备
	fclose(mp3File); // 关闭文件句柄

#if _OUTPUT_TO_FILE_
	fclose(pcmFile);
#endif
	return 0;
}