/******************************************************************
 * Copyright (C) 2024 by ZheJiang Lab. All rights reserved.
 * udp_demo.cpp
 * 之江实验室基于udp 通信
 * created: zhangwenguang
 * date:8月30日 2024年
 * version:0.0.1
 *****************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sched.h>
#include <values.h>
#include <assert.h>
#include <iostream>
#include <vector>
#include <sys/syscall.h>
#include <sys/sysinfo.h>

//#define TimeDomain 1

#ifdef TimeDomain
#define VIDF_SIZE 2416                //VDIF包大小
#define VDIF_NUM 307200               //1s接收的VDIF包
#define DATA_BLOCK_NUM 100
#else
#define VIDF_SIZE 6416                //VDIF包大小
#define VDIF_NUM 128000               //1s接收的VDIF包
#define DATA_BLOCK_NUM 200
#endif
     

#define FILE_SIZE  (VIDF_SIZE * VDIF_NUM)   //1s保存的文件大小

using namespace std;

typedef unsigned int uint32;
typedef unsigned short uint16;

size_t file_num = 20;
int udp_time = 10;                      //默认保存10s
char *dst_addr = "192.168.4.10";
char *port = "7174";
int service_flag = 1;
int thread_num = 1;
bool thread_flag = true;
struct Timer
{
	Timer() {}
	//cpu计时
	void StartTimer();
	double GetTimer();
  private:
      timeval TimerStart;
};
/*linux计时函数类,包括CPU计时和GPU计时*/
void Timer::StartTimer()
{
  gettimeofday(&TimerStart,NULL);
}
//返回值为ms
double Timer::GetTimer()
{
  struct timeval TimerStop, TimerElapsed;
  gettimeofday(&TimerStop, NULL);
  timersub(&TimerStop, &TimerStart, &TimerElapsed);
  return TimerElapsed.tv_sec*1000.0+TimerElapsed.tv_usec/1000.0;
}

struct header_word0
{
  uint32 second : 30;
  uint32 bit : 1;
  uint32 valid : 1;
};

struct header_word1
{
  uint32 frame_sequence : 24;    
  uint32 time_flag : 6;
  uint32 not_set : 2;
};

struct header_word2
{
  uint32 frame_size : 24;    
  uint32 chn_num : 5;
  uint32 version : 3;
};

struct header_word3
{
    uint32 id : 10;
    uint32 board_id : 5;
    uint32 chn_idx : 11;
    uint32 width : 5;
    uint32 complex : 1;
};

struct  data_body
{
#ifdef TimeDomain
  uint16 low_bits[12];
#else
  uint16 low_bits[16];
#endif
  //uint32 high_bits;
};

struct vdif_data_flow
{
  header_word0 word0;
  header_word1 word1;
  header_word2 word2;
  header_word3 word3;
  data_body data[DATA_BLOCK_NUM];
};

int SaveFile(unsigned char * data, int file_num, char * dst_addr, size_t len)
{
  char file_name[64] = {0};
#ifdef TimeDomain
  sprintf(file_name, "data/FPGA_DATA_%s/vdif_160_1024_%d.bin", "TIME", file_num);
#else
  sprintf(file_name, "data/FPGA_DATA_%s/vdif_160_1024_%d.bin", "FREQ", file_num);
#endif
  FILE *fp = fopen(file_name, "wb");
  if (fp == NULL) 
  {
    perror("Error opening file");
    printf("file_name is %s \n", file_name);
    return EXIT_FAILURE;
  }

  // 将结构体写入文件
  if (fwrite(data, len, 1, fp) != 1) 
  {
    fputs("Error writing to file\n", stderr);
    return EXIT_FAILURE;
  }
  // 关闭文件
  fclose(fp);
  return 0;
}
//申请一大块数据
unsigned char * GetMem(size_t file_num)
{
  printf("FILE_SIZE : %d MB\n", FILE_SIZE / 1024 / 1024);
  unsigned char * Buf_p = NULL;
  Buf_p = (unsigned char *)malloc(file_num * FILE_SIZE);
  printf("Buf_p 0x%x\n", Buf_p);
  return Buf_p;
}

void FreeMem(unsigned char ** Buf_p, size_t file_num)
{
  if(Buf_p)
  {
    for(size_t i = 0; i < file_num; i++)
    {
      printf("i %d Buf_p[i] %x \n", i, Buf_p[i]);
      if(Buf_p[i])
        free(Buf_p[i]);
    }
    printf("0000000000\n");
    free(Buf_p);
  }
  return;
}

//svr 接收数据
void * svr_run(void * arg)
{
  int svr_port = * (int *)arg;
  int sockFd,ret,size;
  char on=1;
  sockFd=socket(AF_INET,SOCK_DGRAM,0);
  if(sockFd<0)
  {
    puts("Create socket failed!");
    return nullptr;
  }
  size=sizeof(sockaddr_in);
  sockaddr_in saddr,raddr;
  saddr.sin_family=AF_INET;
  saddr.sin_addr.s_addr=inet_addr(dst_addr);
  saddr.sin_port=htons(svr_port);
  //设置端口复用
  setsockopt(sockFd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

  //绑定地址信息
  ret=bind(sockFd,(sockaddr*)&saddr,size);
  if(ret<0)
  {
    puts("Bind failed!");
    return nullptr;
  }
  int val=sizeof(sockaddr);
  size_t data_size = sizeof(vdif_data_flow);
  printf("sizeof(vdif_data_flow) is %d \n", data_size);
  size_t sum = 0;
  unsigned int frame_id = 0;
  unsigned int tmp = 0;

  //申请一块超大内存
  size_t buffer_size = udp_time * VDIF_NUM;
  unsigned char * buf_p = GetMem(udp_time);
  vdif_data_flow *  rbuf = (vdif_data_flow *)buf_p;
  //vdif_data_flow *  rbuf = (vdif_data_flow *)malloc(data_size);
  printf("rbuf 0x%x \n", rbuf);
  size_t file_idx = 0;
  int time_flag = 0;
  Timer udp_timer;
  printf("listen port: %d ...\n", svr_port);
  //循坏接收消息
  while(1)
  {
    ret=recvfrom(sockFd,rbuf,data_size,0, (sockaddr*)&raddr,(socklen_t*)&val);
    if(ret<0)
    {
        perror("recvfrom failed!");
    }
    if(!time_flag)
    {
      time_flag = 1;
      udp_timer.StartTimer();
    }
    sum += 1;
    if(buffer_size < 0)
    {
      printf("recv rbuf: %s\n", rbuf);
      break;
    }
    if(sum % VDIF_NUM == 0)
    {
      time_flag = 0;
      double time_result = udp_timer.GetTimer() / 1000.0;
      fprintf(stderr,"udp_port: %d cost time: %fsec udp speed :%fGbit/s\n", svr_port, time_result,
          double(VDIF_NUM / 1024.0/1024.0) * VIDF_SIZE * 8.0 / 1024.0 /  time_result);
    }
    if(sum >= buffer_size)
    {
      break;
    }
    rbuf+=1;
  }
  close(sockFd);//关闭udp套接字

  //printf("svr_port %d start save file \n", svr_port);
  //保存成文件
  //SaveFile(buf_p, 0, dst_addr,udp_time * FILE_SIZE);
  for(size_t i = 0; i < udp_time; i++)
  {
    printf("svr_port %d save file: %d \n", svr_port, i);
    SaveFile(buf_p + i * FILE_SIZE, i, dst_addr,FILE_SIZE);
  }
  free(buf_p);
  thread_flag = false;
  return nullptr;
}

//client 发送数据
int client_run(char *dst_addr, char * port, size_t buffer_size)
{
  int sockFd=socket(AF_INET,SOCK_DGRAM,0);
  if(sockFd<0)
  {
      puts("Create socket failed!");
      return -1;
  }
    sockaddr_in saddr,raddr;
    int size=sizeof(saddr);
    saddr.sin_family=AF_INET;
    saddr.sin_addr.s_addr=inet_addr(dst_addr);
    saddr.sin_port=htons(atoi(port));
 
    char on=1;
    int val=sizeof(sockaddr);
    vdif_data_flow * sbuf;
    setsockopt(sockFd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
    int data_size = sizeof(vdif_data_flow);
    sbuf = (vdif_data_flow *)malloc(data_size);
    size_t sum = 0;
    unsigned char * tmp;
    while(1)
    {
      tmp = (unsigned char *)&(sbuf->word1)+3;
      tmp[0] = sum % 160;
      sum++;
      if(buffer_size < 0)
      {
        sprintf((char *)sbuf, "hello world!\n");
      }
      int ret=sendto(sockFd, sbuf, data_size, 0, (sockaddr*)&saddr, val);
      if(ret<0)
      {
          perror("Send data failed!");
      }
      if(sum >= buffer_size)
      {
        break;
      }
    }
    close(sockFd);
    free(sbuf);
    return 0;
}

int main(int argc, char **argv)
{
  int op, ret;
	while ((op = getopt(argc, argv, "s:b:B:C:S:p:T:")) != -1) 
    {
		switch (op) 
        {
		case 's':
			dst_addr = optarg;
      printf("dst_addr %s \n ", dst_addr);
			break;
    case 'b':
			service_flag = 0;
      printf("service_flag %d \n ", service_flag);
			break;
		case 'B':
			file_num = atoi(optarg);
      printf("file_num %d \n ", file_num);
			break;
    case 'T':
			udp_time = atoi(optarg);
      printf("udp_time %d s\n ", udp_time);
			break;
    case 'C':
			thread_num = atoi(optarg);
      printf("thread_num %d s\n ", thread_num);
			break;
		case 'p':
			port = optarg;
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-s server_address]\n");
			printf("\t[-b bind_address]\n");
			printf("\t[-B buffer_size]\n");
			printf("\t[-C transfer_count]\n");
			printf("\t[-S transfer_size]\n");
			printf("\t[-p port_number]\n");
			printf("\t[-T test_option]\n");
			printf("\t    s|sockets - use standard tcp/ip sockets\n");
			printf("\t    a|async - asynchronous operation (use poll)\n");
			printf("\t    b|blocking - use blocking calls\n");
			printf("\t    n|nonblocking - use nonblocking calls\n");
			printf("\t    e|echo - server echoes all messages\n");
			exit(1);
		}
	}
	if(service_flag)
  {
    pthread_t tid;
    cpu_set_t mask;
    for(int i = 0; i < thread_num; i++)
    {
      CPU_ZERO(&mask);
      CPU_SET(i, &mask);
      int svr_port = 7174 + i;
      pthread_create(&tid, NULL, svr_run, (void *)&svr_port);
      pthread_setaffinity_np(tid, sizeof(mask), &mask);
    }
    while (1)
    {
      if(!thread_flag)
      {
        break;
      }
      sleep(1);
    }
  }
  else
  {
    client_run(dst_addr, port, file_num);
  }
	return ret;
}