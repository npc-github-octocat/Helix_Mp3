- 程序基于helix解码库完成MP3音频解码。
- `helix_mp3_for_windows` 程序运行环境为windows，通过读取本地mp3文件并完成解码工作，将PCM数据发送给电脑声卡，实现音频播放。
- `helix_mp3_for_esp32` 程序运行环境为NodeMCU32S，通过网络通信的方式完成mp3数据传输，在NodeMCU32S上完成解码工作以及音频播放。
- 因为需要网络传输，所以`helix_mp3_for_esp32` 需要搭配`ftp_server`服务器程序一起使用。
- 具体说明参考：[https://blog.csdn.net/weixin_42258222/article/details/122640413](https://blog.csdn.net/weixin_42258222/article/details/122640413) 🚀
