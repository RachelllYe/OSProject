#include<stdio.h>
#include<stdlib.h>
#include<stdbool.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<fcntl.h>
#include<time.h>
#include<string.h>

#define times 100
#define maxline (1024*1024)
#define filesize (300*1024*1024)
#define buffsize (1024*1024*1024)

char * filepathDisk[17]={"/usr/file1.txt","/usr/file2.txt","/usr/file3.txt","/usr/file4.txt","/usr/file5.txt","/usr/file6.txt","/usr/file7.txt","/usr/file8.txt","/usr/file9.txt","/usr/file10.txt","/usr/file11.txt","/usr/file12.txt","/usr/file13.txt","/usr/file14.txt","/usr/file15.txt","/usr/file16.txt","/usr/file17.txt"};
//char * filepathDisk[17]={"/home/file1.txt","/home/file2.txt","/home/file3.txt","/home/file4.txt","/home/file5.txt","/home/file6.txt","/home/file7.txt","/home/file8.txt","/home/file9.txt","/home/file10.txt","/home/file11.txt","/home/file12.txt","/home/file13.txt","/home/file14.txt","/home/file15.txt","/home/file16.txt","/home/file17.txt"};
char * filepathRam[17]={"/root/myram/file1.txt","/root/myram/file2.txt","/root/myram/file3.txt","/root/myram/file4.txt","/root/myram/file5.txt","/root/myram/file6.txt","/root/myram/file7.txt","/root/myram/file8.txt","/root/myram/file9.txt","/root/myram/file10.txt","/root/myram/file11.txt","/root/myram/file12.txt","/root/myram/file13.txt","/root/myram/file14.txt","/root/myram/file15.txt","/root/myram/file16.txt","/root/myram/file17.txt"};
char buff[maxline]="aaaaaaaa";
char readbuff[buffsize];

//write函数:
//头文件:#include<unistd.h>
//write(int fd,const void*buf,size_ count)
//fd是对应的打开文件的文件描述符
//buf为需要写入的字符串
//count为每次写入的字节数

//open函数参数
//O_RDWR 读写打开; O_CREAT 若文件不存在则创建; 
//O_SYNC write/read操作为同步模式 
//1KB=1024B(B=byte)

//blocksize表示写的块大小
//isrand=true表示随机写，否则为顺序写
//filepath为文件写的路径
void write_file(int blocksize, bool isrand, char *filepath){
    int fp=open(filepath,O_RDWR|O_CREAT|O_SYNC,0755);
    if(fp==-1) printf("open file error!\n");
    int res;
    //多次重复写入计算时间
    for(int i=0;i<times;i++){
        if((res=write(fp,buff,blocksize))!=blocksize){
            printf("%d\n",res);
            printf("write file error!\n");
        }
        if(isrand){
            //随机生成一个整数 取余定位到文件中任意位置
            //对（filesize-blocksize）取余，防止写出文件
            lseek(fp,rand()%(filesize-blocksize),SEEK_SET);
        }
    }
    lseek(fp,0,SEEK_SET);
}

void read_file(int blocksize,bool isrand,char *filepath){
    int fp=open(filepath,O_RDWR|O_CREAT|O_SYNC,0755);
    if(fp==-1) printf("open file error!\n");
    int res;
    for(int i=0;i<times;i++){
        if((res=read(fp,readbuff,blocksize))!=blocksize){
            printf("%d\n",res);
            printf("read file error!\n");
        }
        if(isrand){
            lseek(fp,rand()%(filesize-blocksize),SEEK_SET);
        }
    }
    lseek(fp,0,SEEK_SET);
}

long get_time_left(struct timeval starttime,struct timeval endtime){
    long spendtime;
     //换算成毫秒
    spendtime=(long)(endtime.tv_sec-starttime.tv_sec)*1000+(endtime.tv_usec-starttime.tv_usec)/1000;
    return spendtime;
}

int main(){
    srand((unsigned)time(NULL));
    //clock_t starttime,endtime;
    struct timeval starttime, endtime;
    double spendtime;
    for(int i=0;i<maxline;i+=16){
        strcat(buff,"aaaaaaaaaaaaaaaa");
    }
    for(int blocksize=64;blocksize<=1024*32;blocksize=blocksize*2){
        //for(int Concurrency=7;Concurrency<=15;Concurrency++){
            int Concurrency=7;
            gettimeofday(&starttime, NULL);
            for(int i=0;i<Concurrency;i++){
                if(fork()==0){
                //随机写
                //write_file(blocksize,true,filepathDisk[i]);
                //write_file(blocksize,true,filepathRam[i]);
                
                //顺序写
                //write_file(blocksize,false,filepathDisk[i]);
                //rite_file(blocksize,false,filepathRam[i]);
                
                //随机读
                read_file(blocksize,true,filepathDisk[i]);
                //read_file(blocksize,true,filepathRam[i]);
                
                //顺序读
                //read_file(blocksize,false,filepathDisk[i]);
                //read_file(blocksize,false,filepathRam[i]);
                exit(0);
                }
            }
            //等待所有子进程结束
            //wait失败返回-1
            while(wait(NULL)!=-1);
            gettimeofday(&endtime, NULL);
            spendtime=get_time_left(starttime,endtime)/1000.0;//换算成秒
            int block=blocksize*Concurrency*times;
            //printf("blocksize=%d\n",block);
            printf("blocksize_KB=%.4fKB,speed=%fMB/s\n",(double)blocksize/1024.0,(double)block/spendtime/1024.0/1024.0);
        //}
    }
    return 0;
}