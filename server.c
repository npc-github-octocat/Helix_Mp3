#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/tcp.h>

#define N 500  //文件名和命令最长为500字节
#define TCP_USER_TIMEOUT 18

int keepAlive = 1;
int keepIdle = 5;
int keepInterval = 5;
int keepCount = 3;
int tcp_timeout = 10000;

void commd_ls(int);
void commd_get(int, char *);
void commd_exit(struct sockaddr_in client_addr, int socket_fd);
void setKeepAlive(int sockfd, int attr_on, socklen_t idle_time, socklen_t interval, socklen_t cnt, int send_timeout);
	
int main(int argc, const char *argv[])
{
	int ser_sockfd, cli_sockfd;
	struct sockaddr_in ser_addr,cli_addr;
	int ser_len, cli_len;
	char commd[N] = {0};
	int bReuseaddr = 1;
	
	if((ser_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		printf("Socket Error!\n");
		return -1;
	}

	bzero(&ser_addr, sizeof(ser_addr));
	ser_addr.sin_family = AF_INET;
	ser_addr.sin_addr.s_addr = inet_addr("xxx.xxx.xxx.xxx"); //本地ip地址
//	ser_addr.sin_addr.s_addr = htonl(INADDR_ANY); //本地ip地址
	ser_addr.sin_port = htons(8989);
	ser_len = sizeof(ser_addr);

	//地址快速重用
	if(setsockopt(ser_sockfd, SOL_SOCKET, SO_REUSEADDR, &bReuseaddr, sizeof(int)) < 0){
		printf("Setsockopt Error!\n");
		return -1;
	}


	//将ip地址与套接字绑定
	if((bind(ser_sockfd, (struct sockaddr *)&ser_addr, ser_len)) < 0){
		printf("Bind Error!\n");
		return -1;
	}

	//服务器监听
	if(listen(ser_sockfd, 5) < 0){
		printf("Listen Error!\n");
		return -1;
	}
	
	bzero(&cli_addr, sizeof(cli_addr));
	cli_len = sizeof(cli_addr);

	while(1){
		printf("server>");
		fflush(stdout);
		if((cli_sockfd = accept(ser_sockfd, (struct sockaddr *)&cli_addr, &cli_len)) < 0){
			printf("Accept Error! error_no: %d\n", cli_sockfd);
			exit(1);
		}
		else{
			printf("\nIP: %s ", inet_ntoa(cli_addr.sin_addr));
			printf("Port: %d [connection success]\n", ntohs(cli_addr.sin_port));

			//心跳检测与发送超时等待
			setKeepAlive(cli_sockfd, keepAlive, keepIdle, keepInterval, keepCount, tcp_timeout);
		}

		if(read(cli_sockfd, commd, N) < 0){
			printf("Read Error! May be client TCP disconnection.\n");
			continue;
		}

		printf("recvd [ %s ]\n", commd);

		if(strncmp(commd, "ls", 2) == 0){
			commd_ls(cli_sockfd);
		}
		else if(strncmp(commd, "get", 3) == 0){
			commd_get(cli_sockfd, commd+4);
		}
		else if(strncmp(commd, "put", 3) == 0){
			/*预留接口*/
		}
		else if(strncmp(commd, "exit", 4) == 0){
			commd_exit(cli_addr, cli_sockfd);
		}
		else{
			printf("Error!Command Error!\n");
		}
	}

	return 0;

}


/* 显示文件列表  */
void commd_ls(int sockfd)
{
	DIR * mydir = NULL;
	struct dirent *myitem = NULL;
	char commd[N];
	bzero(commd, N);
	int i = 0;
	
	if((mydir = opendir(".")) == NULL){
		printf("Opendir Error!\n");
		exit(1);
	}

	while((myitem = readdir(mydir)) != NULL){
		if(sprintf(commd, myitem->d_name, N) < 0){ //把文件名写入commd执行的缓冲区
			printf("Sprintf Error!\n");
			exit(1);
		}
		printf("%d. %s\n", i, commd);
		i++;
		if(write(sockfd, commd, N) < 0){
			printf("Write Error!\n");
			exit(1);
		}
	}

	closedir(mydir);
	close(sockfd);
}

/* 实现文件的下载  */
void commd_get(int sockfd, char *filename)
{
	int fd, nbytes;
	char buffer[N];
	bzero(buffer, N);
	int frame = 0;

	printf("get filename: [ %s ]\n", filename);

	if((fd = open(filename, O_RDONLY)) < 0){
		printf("Open file Error!\n");
		buffer[0] = 'N';
		if(write(sockfd, buffer, N) < 0){
			printf("Write Error! At commd_get function 1!\n");
			exit(1);
		}
		return ;
	}

	buffer[0] = 'Y';
	if(write(sockfd, buffer, N) < 0){
		printf("Write Error! At commd_get function 2!\n");
		close(fd);
		exit(1);
	}
	
	while((nbytes = read(fd, buffer, N)) > 0){
		if(write(sockfd, buffer, nbytes) < 0){
			printf("Write Error! At commd_get function 3!\n");
			close(fd);
			exit(1);
		}
		else{
			printf("send ok: %d\n", frame++);
		}


	}

	close(fd);
	close(sockfd);

	return ;
}

/* 作用：当用户开启断连时，服务做最后本次连接处理工作
 * 参数：
 * 		client_addr：用户连接信息
 * 		socket_fd：TCP accept后的文件描述符
 * 返回值：无
 * */
void commd_exit(struct sockaddr_in client_addr, int socket_fd)
{
	close(socket_fd);
	printf("\nIP: %s ", inet_ntoa(client_addr.sin_addr));
	printf("Port: %d [disconnect success]\n", ntohs(client_addr.sin_port));
}


/* 作用: 用于自动检测网络连接是否断开
 * 参数: 
 *       sockfd：  网络连接套接字
 *       attr_on:  为1表示设定keepAlive
 *       idle_time:开始keepAlive探测前的TCP空闲时间
 *       interval: 两次keepAlive探测间的时间间隔
 *       cnt:      判断断开前的keepAlive探测次数
 *       send_timeout: 发送等待时长
 * 返回值：
 *       无
 *
 * */
void setKeepAlive(int sockfd, int attr_on, socklen_t idle_time, socklen_t interval, socklen_t cnt, int send_timeout)
{
	int result = 0;
	setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&attr_on, sizeof(attr_on));
	setsockopt(sockfd, SOL_TCP, TCP_KEEPIDLE, (const char *)&idle_time, sizeof(idle_time));
	setsockopt(sockfd, SOL_TCP, TCP_KEEPINTVL, (const char *)&interval, sizeof(interval));
	setsockopt(sockfd, SOL_TCP, TCP_KEEPCNT, (const char *)&cnt, sizeof(cnt));

	//发送超时等待，当超过10s未发送成功，自动退出当前状态
	if(result = setsockopt(sockfd, SOL_TCP, TCP_USER_TIMEOUT, &send_timeout, sizeof(int)) < 0){
		error("TCP_USER_TIMEOUT");
	}
}

