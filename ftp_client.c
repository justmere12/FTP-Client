#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <time.h>
#include <utime.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ftp_client.h"

static const char* get_file_mdtm(const char* path)
{
	static char mdtm[15];
	
	struct stat buf;
	if(stat(path,&buf))
	{
		return NULL;
	}
	
	struct tm* tm = localtime(&buf.st_mtim.tv_sec);	
	sprintf(mdtm,"%04d%02d%02d%02d%02d%02d",
		tm->tm_year+1900,
		tm->tm_mon+1,
		tm->tm_mday,
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
	
	return mdtm;
}

static void set_file_mdtm(const char* path,const char* mdtm)
{
	struct stat buf;
	if(stat(path,&buf))
	{
		return;
	}
	
	struct tm tm;
	sscanf(mdtm,"%4d%2d%2d%2d%2d%2d",
		&tm.tm_year,
		&tm.tm_mon,
		&tm.tm_mday,
		&tm.tm_hour,
		&tm.tm_min,
		&tm.tm_sec);
		
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	
	struct utimbuf times;
	times.modtime = mktime(&tm);
	times.actime = buf.st_atim.tv_sec;
	
	utime(path,&times);
}

static uint32_t get_file_size(const char* path)
{
	int fd = open(path,O_RDONLY|O_APPEND);
	if(0 > fd)
	{
		return -1;
	}
	
	return lseek(fd,0,SEEK_END);
}

// 统一使用的从命令通道接收命令的执行结果
static void recv_result(FTPClient* ftp)
{
	int index = 0;
	// 使用\n作为结束标志，是为了防止粘包
	do{
		if(0 > recv(ftp->cfd,ftp->rbuf+index,1,0))
		{
			sprintf(ftp->rbuf,"recv:%m\n");
			ftp->code = -1;
			return;
		}
	}while('\n' != ftp->rbuf[index++]);
	ftp->rbuf[index] = '\0';
	
	// 获取结果状态码
	sscanf(ftp->rbuf,"%d",&ftp->code);
}

// 统一使用的向服务器发送命令的函数
static void send_cmd(FTPClient* ftp)
{
	if(0 > send(ftp->cfd,ftp->sbuf,strlen(ftp->sbuf),0))
	{
		sprintf(ftp->rbuf,"send:%m\n");
		ftp->code = -1;
		return;
	}
	
	// 接收命令执行结果
	recv_result(ftp);
}

FTPClient* create_ftp(void)
{
	FTPClient* ftp = malloc(sizeof(FTPClient));
	ftp->sbuf = malloc(BUF_SIZE);
	ftp->rbuf = malloc(BUF_SIZE);
	
	ftp->cfd = socket(AF_INET,SOCK_STREAM,0);
	if(0 > ftp->cfd)
	{
		perror("socket");
		free(ftp);
		return NULL;
	}

	ftp->islogin = false;
	ftp->isget = false;
	ftp->isput = false;
	return ftp;
}

void connect_ftp(FTPClient* ftp,const char* ip,short port)
{
	struct sockaddr_in addr = {AF_INET};
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	if(connect(ftp->cfd,(struct sockaddr*)&addr,sizeof(addr)))
	{
		sprintf(ftp->rbuf,"Connect:%m\n");
		return;
	}

	recv_result(ftp);
}

void user_ftp(FTPClient* ftp,char* arg)
{
	sprintf(ftp->sbuf,"USER %s\r\n",arg);
	send_cmd(ftp);
}

void pass_ftp(FTPClient* ftp,char* arg)
{
	sprintf(ftp->sbuf,"PASS %s\r\n",arg);
	send_cmd(ftp);
	
	if(230 == ftp->code)
	{
		ftp->islogin = true;
		pwd_ftp(ftp);
	}
}

void pwd_ftp(FTPClient* ftp)
{
	sprintf(ftp->sbuf,"PWD\r\n");
	send_cmd(ftp);
	if(257 == ftp->code)
	{
		sscanf(ftp->rbuf,"%*d \"%s",ftp->remote_path);
		ftp->remote_path[strlen(ftp->remote_path)-1] = '\0';
	}
}

void pasv_ftp(FTPClient* ftp)
{
	sprintf(ftp->sbuf,"PASV\r\n");
	send_cmd(ftp);

	if(227 != ftp->code)
		return;

	printf("%s",ftp->rbuf);

	ftp->dfd = socket(AF_INET,SOCK_STREAM,0);
	if(0 > ftp->dfd)
	{
		sprintf(ftp->rbuf,"socket:%m\n");
		ftp->code = -1;
		return;
	}

	struct sockaddr_in addr = {AF_INET};
	uint8_t* port = (uint8_t*)&addr.sin_port;
	uint8_t* ip = (uint8_t*)&addr.sin_addr.s_addr;
	sscanf(strchr(ftp->rbuf,'(')+1,"%hhu,%hhu,%hhu,%hhu,%hhu,%hhu",
		ip,ip+1,ip+2,ip+3,port,port+1);
		
	if(connect(ftp->dfd,(struct sockaddr*)&addr,sizeof(addr)))
	{
		sprintf(ftp->rbuf,"Connect:%m\n");
		return;
	}
}

void read_to_write(int rfd,int wfd)
{
	char buf[BUF_SIZE];
	//while(write(wfd,buf,read(rfd,buf,BUF_SIZE)));
	
	int ret = 0;
	while((ret = read(rfd,buf,1)))
	{
		write(wfd,buf,ret);
		/*
		sleep(1);
		printf("*");
		fflush(stdout);
		*/
	}
}

void ls_ftp(FTPClient* ftp)
{
	pasv_ftp(ftp);
	
	sprintf(ftp->sbuf,"LIST -al\r\n");
	send_cmd(ftp);
	
	if(150 != ftp->code)
	{
		close(ftp->dfd);
		return;
	}
	
	read_to_write(ftp->dfd,STDOUT_FILENO);
	close(ftp->dfd);
	recv_result(ftp);
}

void cd_ftp(FTPClient* ftp,char* arg)
{
	sprintf(ftp->sbuf,"CWD %s\r\n",arg);
	send_cmd(ftp);
		
	pwd_ftp(ftp);
}

void dele_ftp(FTPClient* ftp,char* arg)
{	
	sprintf(ftp->sbuf,"DELE %s\r\n",arg);
	send_cmd(ftp);		
}

void mkdir_ftp(FTPClient* ftp,char* arg)
{
	sprintf(ftp->sbuf,"MKD %s\r\n",arg);
	send_cmd(ftp);	
}

void rmdir_ftp(FTPClient* ftp,char* arg)
{
	sprintf(ftp->sbuf,"RMD %s\r\n",arg);
	send_cmd(ftp);	
}

void get_ftp(FTPClient* ftp,char* arg)
{	
	// 设置二进制文件传输模式
	sprintf(ftp->sbuf,"TYPE I\r\n");
	send_cmd(ftp);
	
	// 获取文件的字节数
	sprintf(ftp->sbuf,"SIZE %s\r\n",arg);
	send_cmd(ftp);
	if(213 != ftp->code)
		return;
	
	// 备份文件的字节数
	sscanf(ftp->rbuf,"%*d %u",&ftp->file_size);
	
	// 获取文件的最后修改时间
	sprintf(ftp->sbuf,"MDTM %s\r\n",arg);
	send_cmd(ftp);
	if(213 != ftp->code)
		return;
	
	// 备份文件的最后修改时间
	sscanf(ftp->rbuf,"%*d %s",ftp->file_mdtm);
	
	// 判断是否需要续传 字节数不同 本地 < 服务器 && 文件的最后修改时间相同
	int wfd = -1;
	if(get_file_size(arg) < ftp->file_size && 
		0 == strcmp(ftp->file_mdtm,get_file_mdtm(arg)))
	{
		sprintf(ftp->sbuf,"REST %u\r\n",get_file_size(arg));
		send_cmd(ftp);
		wfd = open(arg,O_WRONLY|O_APPEND);
		printf("开启断点续传！\n");
	}
	else
	{
		wfd = open(arg,O_WRONLY|O_CREAT,0644);
	}
	
	if(0 > wfd)
	{
		sprintf(ftp->rbuf,"%s open:%m\n",arg);
		close(ftp->dfd);
		return;
	}
	
	// 标记正在下载
	ftp->isget = true;
	
	// 打开数据通道
	pasv_ftp(ftp);
	
	// 备份文件名
	strcpy(ftp->file_name,arg);
	
	// 告诉服务端开始传输文件内容
	sprintf(ftp->sbuf,"RETR %s\r\n",arg);
	send_cmd(ftp);	

	if(150 != ftp->code)
	{
		close(ftp->dfd);
		return;
	}
	
	// 从服务端读取数据并写入文件
	read_to_write(ftp->dfd,wfd);
	
	// 关闭数据通道、文件
	close(ftp->dfd);
	close(wfd);
	
	// 设置文件的最后修改时间 
	set_file_mdtm(arg,ftp->file_mdtm);
	
	// 取消下载标记
	ftp->isget = false;
	
	recv_result(ftp);
}

void put_ftp(FTPClient* ftp,char* arg)
{
	// 备份文件名
	strcpy(ftp->file_name,arg);
	
	// 设置二进制文件传输模式
	sprintf(ftp->sbuf,"TYPE I\r\n");
	send_cmd(ftp);
	
	// 获取文件的字节数
	sprintf(ftp->sbuf,"SIZE %s\r\n",arg);
	send_cmd(ftp);
	
	int rfd;
	if(550 == ftp->code)
	{
		// 服务器不存在该文件，直接执行正常的上传流程
		rfd = open(arg,O_RDONLY);
		if(0 > rfd)
		{
			sprintf(ftp->rbuf,"%s open:%m\n",arg);
			return;
		} 
	}
	else if(213 == ftp->code)
	{
		// 服务器存在同文件 
		
		// 解析文件的字节数
		uint32_t remote_file_size;
		sscanf(ftp->rbuf,"%*d %u",&remote_file_size);
	
		// 获取文件的最后修改时间
		sprintf(ftp->sbuf,"MDTM %s\r\n",arg);
		send_cmd(ftp);
		if(213 != ftp->code)
			return;
		
		// 解析文件的最后修改时间
		sscanf(ftp->rbuf,"%*d %s",ftp->file_mdtm);
	
		// 断点续传的条件达成 本地和服务上的文件最后修改时间相同 且 服务器上的文件字节数小于本地文件的字节数，所以服务器上的文件内容不完整，需要续传
		if(0 == strcmp(get_file_mdtm(arg),ftp->file_mdtm) && get_file_size(arg) > remote_file_size)
		{
			rfd = open(arg,O_RDONLY);
			if(0 > rfd)
			{
				sprintf(ftp->rbuf,"%s open:%m\n",arg);
				return;
			}
			
			// 设置文件指针，从remote_file_size开始上传
			lseek(rfd,remote_file_size,SEEK_SET);
			
			// 告诉服务器，从remote_file_size开始接收
			sprintf(ftp->sbuf,"REST %u\r\n",remote_file_size);
			send_cmd(ftp);
			if(350 != ftp->code)
				return;
		}
		else
		{
			// 文件同名，但与本地文件不是同一份
			printf("服务器已经有同名文件，是否覆盖(y/n)？");
			char cmd = getchar();
			if('y' != cmd)
				return;
			
			rfd = open(arg,O_RDONLY);
			if(0 > rfd)
			{
				sprintf(ftp->rbuf,"%s open:%m\n",arg);
				return;
			}
		}
	}
	
	// 打开数据通道
	pasv_ftp(ftp);
	
	// 记录上传标志
	ftp->isput = true;
	
	// 告诉服务器准备接收从数据通道接收文件内容 
	sprintf(ftp->sbuf,"STOR %s\r\n",arg);
	send_cmd(ftp);

	if(150 != ftp->code)
	{
		close(ftp->dfd);
		close(rfd);
		return;
	}

	// 读文件内容然后往数据通道发送
	read_to_write(rfd,ftp->dfd);
	// 关闭数据通道
	close(ftp->dfd);

	recv_result(ftp);
	
	// 设置服务端文件的最后修改时间
	sprintf(ftp->sbuf,"MDTM %s %s/%s\r\n",get_file_mdtm(arg),ftp->remote_path,arg);
	send_cmd(ftp);

	// 取消上传标志
	ftp->isput = false;
	
	
/*
	int rfd = open(arg,O_RDONLY);
	if(0 > rfd)
	{
		sprintf(ftp->rbuf,"%s open:%m\n",arg);
		return;
	} 

	pasv_ftp(ftp);
	
	sprintf(ftp->sbuf,"STOR %s\r\n",arg);
	send_cmd(ftp);

	if(150 != ftp->code)
	{
		close(ftp->dfd);
		close(rfd);
		return;
	}

	read_to_write(rfd,ftp->dfd);
	close(ftp->dfd);
	recv_result(ftp);
*/
}

void bye_ftp(FTPClient* ftp)
{
	// 判断是否有正在下载的文件
	if(ftp->isget)
	{
		// 关闭数据通道
		close(ftp->dfd);
		recv_result(ftp);
		puts(ftp->rbuf);
		
		// 设置该文件的最后修改时间
		set_file_mdtm(ftp->file_name,ftp->file_mdtm);
		printf("异常退出，设置文件的最后修改时间！\n");
	}

	if(ftp->isput)
	{
		// 关闭数据通道
		close(ftp->dfd);
		recv_result(ftp);
		puts(ftp->rbuf);
		
		// 设置服务端文件的最后修改时间，这个步骤是能否续传的关键
		sprintf(ftp->sbuf,"MDTM %s %s/%s\r\n",get_file_mdtm(ftp->file_name),ftp->remote_path,ftp->file_name);
		
		send_cmd(ftp);
		puts(ftp->rbuf);
	}
	
	// 关闭命令通道
	close(ftp->cfd);
	
	// 释放内存
	free(ftp->sbuf);
	free(ftp->rbuf);
	free(ftp);
	
	printf("\nGoodbye.\n");
	
	// 结束进程
	exit(EXIT_SUCCESS);
}
