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
#define MP3BUFFER_SIZE 2304                        //����֡һ֡���������2304*2�ֽ�
char readBuf[READBUF_SIZE];
short output[MP3BUFFER_SIZE];
char* mp3Path = "D:\\CloudMusic\\�򵥰�.mp3";      //ϵͳ��(C:)���޷��򿪣����Է���D��
char* pcmPath = "C:\\Users\\xxx\\Desktop\\2.pcm";

int main(int argc, const char *argv[])
{
	HMP3Decoder hMP3Decoder;         // mp3������ָ��
	MP3FrameInfo mp3FrameInfo;       // mp3֡��Ϣ
	int frames = 0;                  // ��¼����֡����
	int bytesLeft = 0;               // ��mp3�ļ����뻺�����е�ʣ���ֽ���
	int offset = 0;                  // ��ƫ��ָ��
	int err = 0, outputSamps = 0;
	char* readPtr = readBuf;
	FILE* mp3File;
	FILE* pcmFile;
	MP3_TYPE mp3player = {
		.bitsPerSample = 16,
		.nChans = 2,
		.samprate = 44100
	};

	/* ��ʼ��MP3������ */
	hMP3Decoder = MP3InitDecoder();
	if (hMP3Decoder == 0) {
		printf("��ʼ��helix������豸\n");
		return -1;
	}
	printf("��ʼ��helix������豸��ɣ�����\n");

	/* ��MP3�ļ� */
	mp3File = fopen(mp3Path, "rb");
	if (mp3File == NULL) {    //�ж��ļ��Ƿ�������
		printf("fail to open %s: %s\n", mp3Path, strerror(errno));
		return -1;
	}
	

#if _OUTPUT_TO_FILE_
	/* ����or��PCM�ļ� */
	pcmFile = fopen(pcmPath, "wb");
	if (pcmFile == NULL) {   //�ж��ļ��Ƿ�������
		printf("fail to open %s: %s\n", pcmPath, strerror(errno));
		fclose(mp3File);
		return;
	}
#endif

	/* ������������ */
	//44100��ʱ����Ҫ���ݸ�����ʵ�ʲ���������д��16λ��ȣ�˫����
	Set_WIN_Params(NULL, mp3player.samprate, mp3player.bitsPerSample, mp3player.nChans);

	/* ��ȡMP3�ļ� */
	int br = fread(readBuf, 1, READBUF_SIZE, mp3File);
	if ((br <= 0)) {  // ��ȡʧ��
		printf("read <%s> fail\r\n", mp3Path);
		MP3FreeDecoder(hMP3Decoder);
		return -1;
	}

	bytesLeft += br;

	while (1) {
		/* find start of next mp3 frame - assume EOF if no sync found */
		offset = MP3FindSyncWord(readPtr, bytesLeft);
		if (offset < 0) {  // û�ҵ�����֡��һ�㶼���ҵ����Ͼ�ID3V2���ݲ��ࣩ
#if _DEBUG_
			printf("��û�ҵ�����֡��readPtr = 0x%p, bytesLeft = %d\n", readPtr, bytesLeft);
#endif
			br = fread(readBuf, 1, READBUF_SIZE, mp3File);
			if ((br <= 0)) {  // ��ȡʧ��
				printf("read <%s> fail\r\n", mp3Path);
				break;
			}
			readPtr = readBuf;
			bytesLeft = br;
			continue;
		}
		else { //�ҵ�����֡
#if _DEBUG_
			printf("���ҵ�����֡��readPtr = 0x%p, bytesLeft = %d, offset = %d\n", readPtr, bytesLeft, offset);
#endif
			readPtr += offset;     // ƫ����ͬ���ֵ�λ��
			bytesLeft -= offset;   // ͬ����֮������ݴ�С�����ܺ��������֡����һ������֡��

			/* ��������֡ */
			if (bytesLeft < 3000) {// ������?��1024Ҳ�����������ŵģ�����NodeMCU32S�ϲ���
				memmove(readBuf, readPtr, bytesLeft);
				br = fread(readBuf + bytesLeft, 1, READBUF_SIZE - bytesLeft, mp3File);
				if ((br <= 0)) break;

				readPtr = readBuf;
				bytesLeft += br;
#if _DEBUG_
				printf("������������ɡ�readPtr = 0x%p, bytesLeft = %d\n", readPtr, bytesLeft);
#endif
			}

			int errs = MP3Decode(hMP3Decoder, &readPtr, &bytesLeft, output, 0);//����һ�ν���һ֡����֡
			frames++;

			if (err != ERR_MP3_NONE) {   // �������
				switch (err) {
					case ERR_MP3_INDATA_UNDERFLOW:
						printf("ERR_MP3_INDATA_UNDERFLOW\r\n");
						br = fread(readBuf, 1, READBUF_SIZE, mp3File); //���¶��뻺����
						if ((br <= 0)) break;
						readPtr = readBuf;
						bytesLeft = br;
					case ERR_MP3_MAINDATA_UNDERFLOW:
						printf("ERR_MP3_MAINDATA_UNDERFLOW\r\n");
						break;
					default:
						printf("UNKNOWN ERROR:%d\r\n", err);
						// ������֡
						if (bytesLeft > 0)
						{
							bytesLeft--;
							readPtr++;
						}
						break;
				}
			}
			else { // ��������
#if _DEBUG_
				printf("������������frames = %d, readPtr = %p, bytesLeft = %d\n", frames, readPtr, bytesLeft);
#endif
				MP3GetLastFrameInfo(hMP3Decoder, &mp3FrameInfo);  // ��ȡ������Ϣ
				outputSamps = mp3FrameInfo.outputSamps;           // PCM���ݸ���
				if (outputSamps > 0) {
					if (1 == mp3FrameInfo.nChans) {
						//������������Ҫ����һ�ݵ���һ������
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
				printf(" \r\n ��KEY1�л�Ϊ�������");
				printf(" \r\n ��KEY2�л�Ϊ���������");
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
		printf("MP3�������\n");
	}
	else {
		printf("MP3��������쳣\n");
	}
	
	MP3FreeDecoder(hMP3Decoder);
	WIN_Audio_close(); //�ر��豸
	fclose(mp3File); // �ر��ļ����

#if _OUTPUT_TO_FILE_
	fclose(pcmFile);
#endif
	return 0;
}