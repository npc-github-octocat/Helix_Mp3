#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include <stdio.h>
#include <stdlib.h>
#include "pub/mp3dec.h"
#include <WiFi.h>
#include <string.h>
#include <driver/i2s.h>

#define _DEBUG_ 0

#define N 500
#define READBUF_SIZE 3000
#define MP3BUFFER_SIZE 2304  //处理立体声音频数据时，输出缓冲区需要的最大大小为2304*16/8字节(16为PCM数据为16位)

//MP3数据帧信息
typedef struct{
  int samprate;
	int bitsPerSample;
	int nChans;
}MP3_TYPE;

/* NodeMCU32S 连接Wifi的账号和密码 */
const char * ssid = "xxx";
const char * password = "xxx";

/* FTP服务器IP地址和端口号 */
const char * host = "xxx.xxx.xxx.xxx";
const int my_ftpPort = 8989;

/* WiFi连接所需变量 */
WiFiClient client;
char commd[N] = {0};
uint8_t End_Flag = 0;

/* mp3解码所需变量 */
char readBuf[READBUF_SIZE];
short output[MP3BUFFER_SIZE];
char i2s_wr_buffer[MP3BUFFER_SIZE * 2];

/* 函数声明 */
void commd_help(void);
void commd_exit(WiFiClient, char *);
int commd_ls(WiFiClient client, char *);
int commd_get(WiFiClient client, char *, char *);
void mySerial_Recv(char *buffer);
int mp3_play(char *trans_buf, char *buffer);
static void csound_audioInit(int, int);
static void csound_audioDeinit(void);
int mp3_data_recv(char *trans_buf, char *buffer, unsigned int count, int bytesLeft);
int pcm16_to_pcm8_to_dac16(short *buffer_pcm16, char *buffer_dac8, int count);

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);//关闭低电压检测,避免无限重启
  Serial.begin(112500);
  delay(1000);

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.print("connection to Ftp server: ");
  Serial.println(host);
  
  while(1){
    End_Flag = 0;
    bzero(readBuf, READBUF_SIZE);
    bzero(commd, N);
    bzero(output, MP3BUFFER_SIZE);
    bzero(i2s_wr_buffer, MP3BUFFER_SIZE * 2);

    // 创建TCP连接 
    if(!client.connect(host, my_ftpPort)){
      Serial.println("connection Ftp failed, please push RST key and whether open Ftp server");
      while(1);
    }
    else{
      Serial.println("\n[ Ftp connection success ]");
    }

    Serial.printf("ftp>");

    mySerial_Recv(commd);
    Serial.printf("Input Command Is [ %s ]\n", commd);

    if(strncmp(commd, "help", 4) == 0){
      commd_help();
    }
    else if(strncmp(commd, "exit", 4) == 0){
      commd_exit(client, commd);
    }
    else if(strncmp(commd, "ls", 2) == 0){
      commd_ls(client, commd);
    }
    else if(strncmp(commd, "get", 3) == 0){
      commd_get(client, commd, readBuf);
    }
    else{
      printf("Command Is Error! Please Try Again!\n");
    }

    client.stop();

  }
}

/* 作用：打印帮助信息
 * 参数：
 *      无
 * 返回值：
 *      无
*/
void commd_help(void)
{
  Serial.printf("\n=------------------欢迎使用FTP------------------=\n");
  Serial.printf("|                                              |\n");
  Serial.printf("|           help: 显示所有FTP服务器命令           |\n");
  Serial.printf("|           exit: 离开FTP服务器                  |\n");
  Serial.printf("|           ls: 显示FTP服务器的文件列表            |\n");
  Serial.printf("|           get <file>: 从FTP服务器下载文件       |\n");
  Serial.printf("|                                              |\n");
  Serial.printf("|----------------------------------------------|\n");  
}


/* 作用：获取云端文件列表
 * 参数：
 *      client: TCP连接对象,类似于TCP文件描述符
 *      buffer: 数据保存位置
 * 返回值：
 *      无
*/
int commd_ls(WiFiClient client, char *buffer)
{
  char i = 0;
  if(client.write(buffer, N) < 0){
    printf("Write Error!\n");
    return -1;
  }
  
  delay(20);//延时很重要，需要服务器那边将数据处理一下再发送

  //服务器那边多次发送，所以这里的延时要保证好，万一第二次服务器发送的时候,
  //超了一些时间，那么available函数就检测不到缓冲区有数据，直接跳过。
  while((client.connected() == 1) || (client.available()>0)){ 
    if(client.read((uint8_t *)buffer, N) > 0){
      Serial.printf("%d ", i++);
      Serial.printf("%s\n", buffer);
    }
    delay(10);
  }

  Serial.printf("------commd_ls End!------\n");
  return 0;
}

/* 作用：下载文件
 * 参数：
 *      client: TCP连接对象,类似于TCP文件描述符
 *      commd_buffer: 命令数据保存位置
 *      buffer: 数据保存位置
 * 返回值：
 *      无
*/
int commd_get(WiFiClient client, char *commd_buf, char *buffer)
{
  if(client.write(commd_buf, N) < 0){
    printf("Write Error!\n");
    return -1;
  }

  delay(20);

  //判断服务器文件是否正常打开，既要考虑网络延迟，又不能多次读取
  while(client.available()>0){ // 用while却不是if主要考虑，如果因为网络延迟会导致缓冲区中数据为0，则直接跳过
      if(client.read((uint8_t *)commd_buf, N) > 0){
        Serial.printf("%s\n", commd_buf);
        
        if(commd_buf[0] != 'Y'){
          Serial.printf("Can't open this file!\n");
          return -1;
        }
        else{
          break;  // 判断当前是第一次接收到，跳转出去，不然直接在该循环中读取文件内容，导致出现错误
        }
      }
      else{
        Serial.printf("Read error!\n");
        return -1;
      }  
  }

  Serial.printf("File content:\n");

  //开始接收文件并播放
  mp3_play(commd_buf, buffer);

  Serial.printf("\nFile recv end\n");
  
  return 0;
}

/* 退出FTP
 *
*/
void commd_exit(WiFiClient client, char *commd_buffer)
{
  if(client.write(commd_buffer, N) < 0){
    printf("Write Error!\n");
    exit(1);
  }
  printf("Bye-bye! ^-^ \n");  //这接口兼容性，厉害
  Serial.printf("Device is disconnection, you can push RST to reconnect");
  while(1);//待优化：开启低功耗
}

/* 作用：获取串口字符串数据
 * 参数：
 *      buffer: 数据保存位置
 * 返回值：
 *      无
*/
void mySerial_Recv(char *buffer)
{
  int i = 0;
  while(1){
    if(Serial.available()){
      buffer[i] = Serial.read();
      if(buffer[i] == 10){ //遇到换行符作为结束接收标志
        break;
      }
      else{
        i++;
      }
    }
  }
  buffer[i] = '\0';
}


/* I2S初始化
 *
*/
static void csound_audioInit(int sample, int bitpersample)
 {
     i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
      .sample_rate = sample,
      .bits_per_sample = (i2s_bits_per_sample_t)bitpersample,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, //影响数据传输的格式，根据音频文件进行选择
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = 0, // default interrupt priority
      .dma_buf_count = 4,    //这参数调试过，效果相对较好
      .dma_buf_len = 254,    //这参数调试过，效果相对较好
      .use_apll = false
     };
     
     i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL); //install and start i2s driver
     i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN); //可以控制有几个声道出声音
 }
 
 /* I2S卸载
 *
*/
 static void csound_audioDeinit()
 {
      i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
      i2s_driver_uninstall(I2S_NUM_0);
 }

/* 作用：音频播放
 * 参数：
 *      trans_buf: 用于网络传输的缓存，当传输音频数据时，多个trans_buf合并保存到buffer中
 *      buffer：解码前，数据帧保存位置
 * 返回值：
 *      -1：表示出错
 *      0： 表示正常      
*/
int mp3_play(char *trans_buf, char *buffer)
{
  HMP3Decoder hMP3Decoder;         // mp3解码器指针
  MP3FrameInfo mp3FrameInfo;       // mp3帧信息
  int frames = 0;                  // 记录数据帧个数
  int err = 0, outputSamps = 0;
  int bytesLeft = 0;               // 从mp3文件读入缓冲区中的剩余字节数
  int offset = 0;                  // 读偏移指针
  char *readPtr = buffer;
  int errs;
  size_t bytes_written;

  MP3_TYPE mp3player = {
    .samprate = 44100,
    .bitsPerSample = 16,
    .nChans = 2
  };

  csound_audioInit(mp3player.samprate, mp3player.bitsPerSample);  //I2S初始化

  /* 初始化MP3解码器 */
	hMP3Decoder = MP3InitDecoder();
	if (hMP3Decoder == 0) {
		printf("初始化helix解码库设备\n");
    return -1;
	}
	printf("初始化helix解码库设备完成\n");
  
  //接收mp3文件第一组数据
  bytesLeft = mp3_data_recv(trans_buf, buffer, READBUF_SIZE, 0);
  if(-1 == bytesLeft){
    printf("Recv mp3 first data Error!\n");
    return -1;
  }

  while(1){
    errs = 0;
    if(1 == End_Flag) break;
    /* find start of next mp3 frame - assume EOF if no sync found */
	  offset = MP3FindSyncWord((unsigned char *)buffer, bytesLeft);
	  if (offset < 0) {  // 没找到数据帧（一般都能找到，毕竟ID3V2数据不多）
#if _DEBUG_
      printf("【没找到数据帧】readPtr = %p, bytesLeft = %d\n", readPtr, bytesLeft);
#endif
		  //退出重新读
      bytesLeft = mp3_data_recv(trans_buf, buffer, READBUF_SIZE, 0);
      if(-1 == bytesLeft ){
        printf("Recv mp3 data Error!\n");
        return -1;
      }
      readPtr = buffer;
      continue;
	  }
    else { //找到数据帧
#if _DEBUG_
      printf("【找到数据帧】readPtr = %p, bytesLeft = %d, offset = %d\n", readPtr, bytesLeft, offset);
#endif
      readPtr += offset;     // 偏移至同步字的位置
      bytesLeft -= offset;   // ͬ同步字之后的数据大小（可能含多个数据帧或不足一个数据帧）

      /* 补充数据帧 */
      if (bytesLeft < READBUF_SIZE) {
        memmove(buffer, readPtr, bytesLeft);

        //接收数据
        bytesLeft = mp3_data_recv(trans_buf, buffer + bytesLeft, READBUF_SIZE - bytesLeft, bytesLeft);

        if(-1 == bytesLeft){
          printf("Recv mp3 data Error!\n");
          return -1;
        }
        readPtr = buffer;
#if _DEBUG_
          printf("【补充数据完成】readPtr = %p, bytesLeft = %d\n", readPtr, bytesLeft);
#endif
      }

      /* 开始解码 */
      errs = MP3Decode(hMP3Decoder, (unsigned char **)&readPtr, &bytesLeft, output, 0);//调用一次解码一帧数据帧
      frames++;

      if (ERR_MP3_NONE != err) {   // 解码错误
        switch (err) {
          case ERR_MP3_INDATA_UNDERFLOW:
            printf("ERR_MP3_INDATA_UNDERFLOW\r\n");
            bytesLeft = mp3_data_recv(trans_buf, buffer, READBUF_SIZE, 0); //重新填充数据
            readPtr = buffer;
            break;
          case ERR_MP3_MAINDATA_UNDERFLOW:
            printf("ERR_MP3_MAINDATA_UNDERFLOW\r\n");
            break;
          default:
            printf("UNKNOWN ERROR:%d\r\n", err);
            // 跳过此帧
            if (bytesLeft > 0){
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
          if (1 == mp3FrameInfo.nChans){
            //单声道数据需要复制一份到另一个声道
            int i;
            for (i = outputSamps - 1; i >= 0; i--){
              output[i * 2] = output[i];
              output[i * 2 + 1] = output[i];
            }
            outputSamps *= 2;
          }
        }

        pcm16_to_pcm8_to_dac16(output, i2s_wr_buffer, outputSamps);

        if ((mp3player.samprate != mp3FrameInfo.samprate) && (mp3player.bitsPerSample != mp3FrameInfo.bitsPerSample)) {
					mp3player.samprate = mp3FrameInfo.samprate;
					mp3player.bitsPerSample = mp3FrameInfo.bitsPerSample;
					csound_audioInit(mp3player.samprate, mp3player.bitsPerSample);  //I2S初始化
				}

        //音频数据，将通过I2S的方式发送给内部DAC，outputSamps*2 表示待发送的字节数
        i2s_write(I2S_NUM_0, i2s_wr_buffer, outputSamps*2, &bytes_written, 100);
#if _DEBUG_
        printf("【音频数据发送完成】frames = %d, readPtr = %p, bytesLeft = %d\n", frames, readPtr, bytesLeft);
#endif
      }
    }
  }
  printf("frames: %d\n", frames);
  MP3FreeDecoder(hMP3Decoder);
  csound_audioDeinit(); //I2S卸载
  return 0;
}

/* 作用：音频数据网络接收
 * 参数：
 *      trans_buf: 用于网络传输的缓存，多个trans_buf合并保存到buffer中
 *      buffer：解码前，数据帧保存位置
 *      count: 接收数据量，单位为字节
 *      bytesLeft: 有效数据量，单位为字节
 * 返回值：
 *      -1：表示出错
 *      0： 表示正常      
*/
int mp3_data_recv(char *trans_buf, char *buffer, unsigned int count, int bytesLeft)
{
  int br = 0;
  int ret = bytesLeft;
  unsigned int trans_count = 0;

  if((count / N) > 0){
    trans_count = N;
  }
  else{
    trans_count = count;
  }
  
  while((1 == client.connected()) || (client.available() > 0)){
    if((br = client.read((uint8_t *)(trans_buf), trans_count)) < 0){ 
      printf("Recv error\n");
      return -1;
    }
    else{
      memcpy(buffer, trans_buf, br);
      buffer += br;
      ret += br;

      count -= br;
      if(count <= 0) break;

      if((count / N) > 0){
        trans_count = N;
      }
      else{
        trans_count = count;
      }
    }
  }

  //用该标志位作为音频播放结束有点不严谨，可能存在这么一种情况，虽然传输结束了，但是还有最多3000字节的数据帧未处理
  if(0 == client.connected()){
    End_Flag = 1;
  }
  return ret;
}

/* 作用：将PCM16数据转换为PCM8数据，为了适配DAC，将PCM8位数据转换为适合DAC的16位数据
 * 参数：
 *      buffer_pcm16: 解码完成的数据
 *      buffer_dac16：用于I2S发送给DAC的数据
 *      count: pcm16数据量
 * 返回值：音频数据量，单位为半字
 *      
*/
int pcm16_to_pcm8_to_dac16(short *buffer_pcm16, char *buffer_dac16, int count)
{
  char array[MP3BUFFER_SIZE * 2] = {0};
  short tmp16 = 0;
  char tmp8 = 0;
  uint8_t utmp8 = 0;
  for(int i = 0; i <= count; i++){
    tmp16 = *(buffer_pcm16 + i);
    tmp8 = tmp16 >> 8;
    utmp8 = tmp8 + 128;
    array[i] = utmp8;
  }

  //I2S DAC的规则,所以将8位PCM转换成16位数据
  uint32_t j = 0;
  for (int i = 0; i <= count; i++) {
    buffer_dac16[j++] = 0;
    //value=s_buff[i]; //根据音量大小成比例的调整DA输出值的幅度
    buffer_dac16[j++] = array[i];
  }
  return count;
}