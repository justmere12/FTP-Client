#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <utime.h>

const char* get_file_mdtm(const char* path)
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

void set_file_mdtm(const char* path,const char* mdtm)
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

uint32_t get_file_size(const char* path)
{
	int fd = open(path,O_RDONLY|O_APPEND);
	if(0 > fd)
	{
		return -1;
	}
	
	return lseek(fd,0,SEEK_END);
}

int main(int argc,const char* argv[])
{
	int fd = open("test.txt",O_WRONLY);
	if(0 > fd)
	{
		perror("open");
		return -1;
	}

	printf("%u\n",get_file_size("20220915_150501.mp4"));
	printf("%s\n",get_file_mdtm("20220915_150501.mp4"));
	set_file_mdtm("test.txt",get_file_mdtm("20220915_150501.mp4"));
	printf("%s\n",get_file_mdtm("test.txt"));
	char ch = 'Z';
	write(fd,&ch,1);
	close(fd);
}
