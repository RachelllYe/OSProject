#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <curses.h>
#include <limits.h>
#include <termcap.h>
#include <termios.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <dirent.h>

#include <minix/com.h>
#include <minix/config.h>
#include <minix/type.h>
#include <minix/endpoint.h>
#include <minix/const.h>
#include <minix/u64.h>
#include <paths.h>
#include <minix/procfs.h>

#include <sys/stat.h>  //文件状态
#include <sys/types.h> //基本系统数据类型
#include <sys/wait.h>  //进程控制
#include <sys/times.h> //进程时间
#include <sys/time.h>
#include <sys/select.h>

#define MAXLINE 1024 
#define MAXARGS 128  //命令行最大参数数量
#define M 256

#define USED 0x1
#define IS_TASK 0x2
#define IS_SYSTEM 0x4
#define BLOCKED 0x8
#define PSINFO_VERSION 0

#define STATE_RUN 'R'
const char *cputimenames[] = {"user", "ipc", "kernelcall"};
#define CPUTIMENAMES ((sizeof(cputimenames)) / (sizeof(cputimenames[0]))) //恒等于3
#define CPUTIME(m, i) (m & (1 << (i)))                                    //保留第几位

char prompt[] = "myshell> ";
char history[M][M];
int n_his = 0;
char *path = NULL;
unsigned int nr_procs, nr_tasks;
int slot = -1;
int nr_total;

struct proc
{
    int p_flags;
    endpoint_t p_endpoint;           //端点
    pid_t p_pid;                     //进程号
    u64_t p_cpucycles[CPUTIMENAMES]; //CPU周期
    int p_priority;                  //动态优先级
    endpoint_t p_blocked;            //阻塞状态
    time_t p_user_time;              //用户时间
    vir_bytes p_memory;              //内存
    uid_t p_effuid;                  //有效用户ID
    int p_nice;                      //静态优先级
    char p_name[PROC_NAME_LEN + 1];  //名字
};

struct proc *proc = NULL, *prev_proc = NULL;

struct tp
{
    struct proc *p;
    u64_t ticks;
};

//封装好的fork函数
pid_t Fork(void); 
//实现内置命令、program命令和后台运行等功能
void exeCommand(char *cmdline);
//解析命令行解析命令行，得到参数序列，并判断是前台作业还是后台作业
int parseline(const char *cmdline, char **argv); 
//读取/proc/kinfo得到总的进程和任务数num_total
void getkinfo(); 
//在/proc/meminfo中查看内存信息，计算出内存大小并打印
int print_memory();
//计算总体CPU使用占比并打印结果
void print_procs(struct proc *proc1, struct proc *proc2, int cputimemode);
//计算cputicks
u64_t cputicks(struct proc *p1, struct proc *p2, int timemode);
void get_procs();
//读取目录下每一个文件信息
void parse_dir();
//在/proc/pid/psinfo中，查看进程pid的信息
void parse_file(pid_t pid);
//内置命令实现函数
int builtin_cmd(char **argv);
//实现管道，完成进程间的通信
void pipeline(char *process1[],char *process2[]);
void unix_error(char *msg);

int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];

    while (1)
    {
        path = getcwd(NULL, 0);//获取当前工作文件路径
        printf("%s%s# ", prompt, path);
        fflush(stdout);
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) //返回非零值表示出错
            printf("fgets error");
        if (feof(stdin))
        { //检查是否到达文件尾，若返回非零值则代表到达文件尾
            fflush(stdout);
            exit(0);
        }

        for (int i = 0; i < M; i++)
        {
            history[n_his][i] = cmdline[i];
        }
        //printf("%s\n",history[n_his]);
        ++n_his;
        exeCommand(cmdline);
        fflush(stdout);
        fflush(stdout);
    }
    exit(0);
}

void exeCommand(char *cmdline)
{
    char *argv[MAXARGS];
    char buf[MAXLINE];
    int bg;    //进程在前端还是后端运行
    pid_t pid; //进程号
    char *file;
    int fd;
    int status;
    //sigset_t mask_all,mask_one,prev_one;
    int situation = 0; //situation=1(>),2(<),3(|),4(后台)
    strcpy(buf, cmdline);
    bg = parseline(buf, argv); 
    //输入参数：命令行和参数列表
    //根据返回值判断为前台(0)/后台作业(1)，并得到参数列表
    if (bg == 1)
    {
        situation = 4;
    }
    if (argv[0] == NULL)
    {
        return;
    }
    int n=builtin_cmd(argv);
    if(n) return;//内置命令直接返回
    int i = 0;
    for (i = 0; argv[i] != NULL; i++)
    {
        if (strcmp(argv[i], ">") == 0)
        {
            file = argv[i + 1];
            argv[i] = NULL;
            situation = 1;
            break;
        }
    }
    for (i = 0; argv[i] != NULL; i++)
    {
        if (strcmp(argv[i], "<") == 0)
        {
            file = argv[i + 1];
            printf("filename=%s\n", file);
            argv[i] = NULL;
            situation = 2;
            break;
        }
    }
    char *leftargv[MAXARGS];
    for (i = 0; argv[i] != NULL; i++)
    {
        if (strcmp(argv[i],"|")==0)
        {
            situation = 3;
            argv[i] = NULL;
            int j;
            for (j = i + 1; argv[j] != NULL; j++)
            {
                leftargv[j - i - 1] = argv[j];
            }
            leftargv[j - i - 1] = NULL;
            break;
        }
    }

    switch (situation)
    {
    case 0:
        pid = Fork();
        if (pid == 0)//pid==0说明在子进程
        {
            execvp(argv[0], argv);
            exit(0);
        }
        if (waitpid(pid, &status, 0) == -1)//父进程等待结束
        {
            printf("wait for child process error\n");
        }
        break;
    case 1: //(>)
        pid = Fork();
        if (pid == 0)
        {
            fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0644);//得到file(>后)的文件描述符
            if (fd == -1)
            {
                printf("open %s error!\n", file);
            }
            dup2(fd, 1); //将结果输出到file,因此映射为标准输出1标准输出
            close(fd);
            execvp(argv[0], argv);//执行前半部分指令
            exit(0);
        }
        if (waitpid(pid, &status, 0) == -1)
        {
            printf("wait for child process error\n");
        }
        break;
    case 2: //(<)
        pid = Fork();
        if (pid == 0)
        {
            fd = open(file, O_RDONLY);
            dup2(fd, 0); //0标准输入，隐射到标准输入
            close(fd);
            execvp(argv[0], argv);
            exit(0);
        }
        if (waitpid(pid, &status, 0) == -1)
        {
            printf("wait for child process error\n");
        }
        break;
    case 3: //(|)
        if((pid = fork()) < 0){  //创建子进程
            printf("fork error\n");
            return ;
        }
        if (pid == 0)
        {
            pipeline(argv,leftargv);
        }
        else
        {
            if (waitpid(pid, &status, 0) == -1)
            {
                printf("wait for child process error\n");
            }
        }
        break;
    case 4: //(&后台运行)
        pid = Fork();
        signal(SIGCHLD, SIG_IGN);
        if (pid == 0)
        {    
            signal(SIGCHLD, SIG_IGN);
            //如果将此信号的处理方式设为忽略，可让内核把僵尸子进程转交给init进程去处理
            // /dev/null
            //黑洞(black hole)通常被用于丢弃不需要的输出流，或作为用于输入流的空文件
            close(0);
            open("/dev/null",O_RDONLY);
            close(1);
            open("/dev/null",O_WRONLY);
            execvp(argv[0], argv);
            exit(0);
        }
        //后台运行，不用等待结束
        break;
    default:
        break;
    }
    return;
}

int builtin_cmd(char **argv){
    if (!strcmp(argv[0], "exit"))
    { //strcmp若两个字符串相等则返回0
        exit(0);
    }
    if (!strcmp(argv[0], "cd"))
    {
        if (!argv[1])
        {
            argv[1] = ".";
        }
        int ret;
        ret = chdir(argv[1]); //改变工作目录
        if (ret < 0)
        {
            printf("No such directory!\n");
        }
        else
        {
            path = getcwd(NULL, 0); //利用getcwd取当前所在目录
        }
        return 1;
    }
    if (!strcmp(argv[0], "history"))
    {
        if (!argv[1])
        { //当只输入history时，打印已有的所有指令
            for (int j = 1; j <= n_his; j++)
            {
                printf("%d ", j);
                puts(history[j - 1]);
            }
        }
        else
        {
            int t = atoi(argv[1]);
            if (n_his - t < 0)
            { //如果history后未带参数或带的参数大于已有指令数
                 printf("history error\n");
            }
            else
            {
                for (int j = n_his - t; j < n_his; j++)
                {
                    printf("%d ", j + 1);
                    puts(history[j]);
                }
            }
        }
        return 1;
    }
    if (!strcmp(argv[0], "mytop"))
    {
        int cputimemode = 1;//计算CPU的时钟周期
        getkinfo();
        print_memory();
        //得到prev_proc
        get_procs();
        if (prev_proc == NULL)
        {
            get_procs();//得到proc
        }
        print_procs(prev_proc, proc, cputimemode);
        return 1;
    }
    return 0;
}

void pipeline(char *process1[],char *process2[]){
    int fd[2];
    pipe(&fd[0]);
    int status;
    pid_t pid;
    pid=Fork();
    if(pid==0){
        close(fd[0]);
        close(1);
        dup(fd[1]);//fd[1]管道写入端，映射到标准输出1
        close(fd[1]);
        execvp(process1[0],process1);//执行前部分指令，结果输出到管道
    }else{
        close(fd[1]);
        close(0);
        dup(fd[0]);//fd[0]管道读入端，映射到标准输入0
        close(fd[0]);
        //waitpid(pid,&status,0);//等待子进程结束，管道中有内容了，再执行
        execvp(process2[0],process2);//从管道读入
    }
}

//解析命令行，返回值判断是前台左右还是后台作业
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE];
    char *buf = array;
    int argc = 0;
    int bg;

    //char *strtok(char *s,char *delim) 实现原理：将分隔符出现的地方改为'\0'
    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' ';   //将行末尾的回车改为空格
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    char *s = strtok(buf, " ");
    if (s == NULL)
    {
        exit(0);
    }
    argv[argc] = s;
    argc++;
    while ((s = strtok(NULL, " ")) != NULL)
    { //参数设置为NULL，从上一次读取的地方继续
        argv[argc] = s;
        argc++;
    }
    argv[argc] = NULL;

    if (argc == 0) /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[(argc)-1] == '&')) != 0)
    {
        argv[--(argc)] = NULL;
    }
    return bg;
}

/* /proc/kinfo查看进程和任务数量*/
void getkinfo()
{
    FILE *fp;
    if ((fp = fopen("/proc/kinfo", "r")) == NULL)
    {
        fprintf(stderr, "opening /proc/kinfo failed\n");
        exit(1);
    }

    if (fscanf(fp, "%u %u", &nr_procs, &nr_tasks) != 2)
    {
        fprintf(stderr, "reading from /proc/kinfo failed");
        exit(1);
    }

    fclose(fp);

    //nr_total是一个全局变量
    nr_total = (int)(nr_procs + nr_tasks);
}

/*/proc/meminfo查看内存信息*/
int print_memory()
{
    FILE *fp;
    unsigned long pagesize, total, free, largest, cached;

    if ((fp = fopen("/proc/meminfo", "r")) == NULL)
    {
        return 0;
    }

    if (fscanf(fp, "%lu %lu %lu %lu %lu", &pagesize, &total, &free, &largest, &cached) != 5)
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);

    printf("main memory: %ldk total,%ldk free,%ldk contig free,%ldk cached\n", (pagesize * total) / 1024,
           (pagesize * free) / 1024, (pagesize * largest) / 1024, (pagesize * cached) / 1024);

    return 1;
}

void get_procs()
{
    struct proc *p;
    int i;
    //交换了prev_proc&proc
    p = prev_proc;
    prev_proc = proc;
    proc = p;

    if (proc == NULL)
    {
        //proc是struct proc的集合，申请了
        //nr_total个proc的空间
        proc = malloc(nr_total * sizeof(proc[0])); //struct proc的大小
        if (proc == NULL)
        {
            fprintf(stderr, "Out of memory!\n");
            exit(0);
        }
    }

    for (i = 0; i < nr_total; i++)
    {
        proc[i].p_flags = 0; 
    }

    parse_dir();
}

void parse_dir()
{
    DIR *p_dir;
    struct dirent *p_ent; 
    pid_t pid;
    char *end; //指向第一个不可转换的字符位置的指针

    if ((p_dir = opendir("/proc")) == NULL)
    {
        perror("opendir on /proc");
        exit(0);
    }

    //读取目录下的每一个文件信息
    for (p_ent = readdir(p_dir); p_ent != NULL; p_ent = readdir(p_dir))
    {
        //long int strtol (const char* str, char** endptr, int base);
        //将字符串转化为长整数，endptr第一个不可转换的位置的字符指针，base要转换的进制
        //合法字符为0x1-0x9
        pid = strtol(p_ent->d_name, &end, 10);
        //由文件名获取进程号
        //pid由文件名转换得来
        //ASCII码对照表，NULL的值为0
        if (!end[0] && pid != 0)
        {
            parse_file(pid);
        }
    }
    closedir(p_dir);
}

//proc/pid/psinfo
void parse_file(pid_t pid)
{
    //PATH_MAX定义在头文件<limits.h>，对路径名长度的限制
    char path[PATH_MAX], name[256], type, state;
    int version, endpt, effuid;         //版本，端点，有效用户ID
    unsigned long cycles_hi, cycles_lo; //高周期，低周期
    FILE *fp;
    struct proc *p;
    int i;
    //将proc/pid/psinfo路径写入path
    sprintf(path, "/proc/%d/psinfo", pid);

    if ((fp = fopen(path, "r")) == NULL)
    {
        return;
    }

    if (fscanf(fp, "%d", &version) != 1)
    {
        fclose(fp);
        return;
    }

    if (version != PSINFO_VERSION)
    {
        fputs("procfs version mismatch!\n", stderr);
        exit(1);
    }

    if (fscanf(fp, " %c %d", &type, &endpt) != 2)
    {
        fclose(fp);
        return;
    }

    slot++; //顺序取出每个proc让所有task的slot不冲突

    if (slot < 0 || slot >= nr_total)
    {
        fprintf(stderr, "mytop:unreasonable endpoint number %d\n", endpt);
        fclose(fp);
        return;
    }

    p = &proc[slot]; //取得对应的struct proc

    if (type == TYPE_TASK)
    {
        p->p_flags |= IS_TASK; //0x2 倒数第二位标记为1
    }
    else if (type == TYPE_SYSTEM)
    {
        p->p_flags |= IS_SYSTEM; //0x4 倒数第三位标记为1
    }
    p->p_endpoint = endpt;
    p->p_pid = pid;
    //%*u添加了*后表示文本读入后不赋给任何变量
    if (fscanf(fp, " %255s %c %d %d %lu %*u %lu %lu",
               name, &state, &p->p_blocked, &p->p_priority,
               &p->p_user_time, &cycles_hi, &cycles_lo) != 7)
    {
        fclose(fp);
        return;
    }

    //char*strncpy(char*dest,char*src,size_tn);
    //复制src字符串到dest中，大小由tn决定
    strncpy(p->p_name, name, sizeof(p->p_name) - 1);
    p->p_name[sizeof(p->p_name) - 1] = 0;

    if (state != STATE_RUN)
    {
        p->p_flags |= BLOCKED; //0x8 倒数第四位标记为1
    }

    //user的CPU周期
    p->p_cpucycles[0] = make64(cycles_lo, cycles_hi);
    p->p_flags |= USED; //最低位标记位1

    fclose(fp);
}

u64_t cputicks(struct proc *p1, struct proc *p2, int timemode)
{
    int i;
    u64_t t = 0;
    for (i = 0; i < CPUTIMENAMES; i++)
    {
        //printf("i=%d,%d\n",i,CPUTIME(timemode,i));
        if (!CPUTIME(timemode, i))
        {
            continue;
        }
        //printf("run\n");
        //timemode==1只有i等于0是CPUTIME才等于1
        //只有i=0时会执行后面的，即只计算了CPU的时钟周期不会对另外两个做计算
        //p_cpucycles第二个值为ipc，第三个值为kernelcall的数量
        //如果两个相等则作差求时间差
        if (p1->p_endpoint == p2->p_endpoint)
        {
            t = t + p2->p_cpucycles[i] - p1->p_cpucycles[i];
        }
        else
        { //否则t直接加上p2
            t = t + p2->p_cpucycles[i];
        }
    }
    return t;
}

void print_procs(struct proc *proc1, struct proc *proc2, int cputimemode)
{
    int p, nprocs;
    u64_t systemticks = 0;
    u64_t userticks = 0;
    u64_t total_ticks = 0;
    u64_t idleticks = 0;
    u64_t kernelticks = 0;
    int blockedseen = 0;
    //创建了一个struct tp的结构体数组
    static struct tp *tick_procs = NULL;
    if (tick_procs == NULL)
    {
        tick_procs = malloc(nr_total * sizeof(tick_procs[0]));
        if (tick_procs == NULL)
        {
            fprintf(stderr, "Out of memory!\n");
            exit(1);
        }
    }
    for (p = nprocs = 0; p < nr_total; p++)
    {
        u64_t uticks;
        if (!(proc2[p].p_flags & USED))
        { //查看USED位是否被标记
            continue;
        }
        tick_procs[nprocs].p = proc2 + p;//初始化
        //tickprocs的第np个结构体的struct proc *p
        //为proc2第p个文件的struct proc
        tick_procs[nprocs].ticks = cputicks(&proc1[p], &proc2[p], cputimemode);
        uticks = cputicks(&proc1[p], &proc2[p], 1);
        total_ticks = total_ticks + uticks;
        if (p - NR_TASKS == IDLE)
        {
            idleticks = uticks;
            continue;
        }
        if (p - NR_TASKS == KERNEL)
        {
            kernelticks = uticks;
        }
        if (!(proc2[p].p_flags & IS_TASK))
        {
            //如果是进程，则看是system还是user
            if (proc2[p].p_flags & IS_SYSTEM)
            {
                systemticks = systemticks + tick_procs[nprocs].ticks;
            }
            else
            {
                userticks = userticks + tick_procs[nprocs].ticks;
            }
        }
        nprocs++;
    }
    if (total_ticks == 0)
    {
        return;
    }
    printf("CPU states: %6.2f%% user, ", 100.0 * userticks / total_ticks);
    printf("%6.2f%% system, ", 100.0 * systemticks / total_ticks);
    printf("%6.2f%% kernel, ", 100.0 * kernelticks / total_ticks);
    printf("%6.2f%% idle\n", 100.0 * idleticks / total_ticks);
}

void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

//封装好的fork函数
pid_t Fork(void)
{
    pid_t pid;
    if ((pid = fork()) < 0)
    {
        unix_error("Fork error");
    }
    return pid;
}
