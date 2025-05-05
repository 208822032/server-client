#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#define TIME_SUB_MS(tv1,tv2) ((tv1.tv_sec - tv2.tv_sec)*1000 + (tv1.tv_usec - tv2.tv_usec)/1000)


#define MAX_EPOLLSIZE       (384*1024)
#define MAX_PORT             100
#define MAX_BUFFER           128

int isContinue = 0;

static int ntySetNonblock(int fd){
    int flags;

    /*
    fcntl()：全称 "file control"，用于操作文件描述符（fd）的属性
    F_GETFL：命令参数，表示获取文件状态标志（File Status Flags）
    成功时返回文件状态标志的整数值，失败返回 -1（需检查 errno）
    */
    flags = fcntl(fd,F_GETFL,0);
    if(flags < 0) return flags;
    //按位或赋值：|= 运算符将 O_NONBLOCK 标志添加到 flags 的当前值中。
    //启用文件描述符（如套接字）的非阻塞 I/O 模式。
    flags |= O_NONBLOCK;
    if(fcntl(fd,F_SETFL,flags) < 0) return -1;
    return 0;

}

static int ntySetReUseAddr(int fd){
    int reuse = 1;
    //设置套接字选项的核心函数调用，其作用是允许地址重用
    //SO_REUSEADDR：允许套接字绑定到处于 TIME_WAIT 状态的地址和端口，
    //避免“Address already in use”错误。
    /*
    int setsockopt(int fd,             // 套接字文件描述符
               int level,           // 协议级别（此处为 SOL_SOCKET）
               int optname,         // 选项名称（此处为 SO_REUSEADDR）
               const void *optval,  // 选项值的指针
               socklen_t optlen);   // 选项值的大小
    */
    return setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,(char*)&reuse,sizeof(reuse));
}

//
int main(int argc,char** argv){
     if(argc <= 2){
         printf("Usage: %s ip port\n",argv[0]);
         exit(0);
     }

    const char* ip = argv[1];
    int port = atoi(argv[2]);
    // const char* ip = "192.168.36.129";
    // int port = 8888;
    int connections = 0;
    char buffer[128] = {0};
    int i = 0,index = 0;

    struct epoll_event events[MAX_EPOLLSIZE];

    int epoll_fd = epoll_create(MAX_EPOLLSIZE);

    strcpy(buffer," Data From MulClient\n");

    struct sockaddr_in addr = {0};
    memset(&addr,0,sizeof(struct sockaddr_in));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);

    struct timeval tv_begin;
    gettimeofday(&tv_begin,NULL);//获取当前时间 存储结果 时区

    while(1){
        if(++index >= MAX_PORT) index = 0;
        
        struct epoll_event ev;
        int sockfd = 0;

        if(connections < 340000 && !isContinue){
            sockfd = socket(AF_INET,SOCK_STREAM,0);
            if(sockfd < 0){
                perror("socket");
                goto err;
            }

            addr.sin_port = htons(port+index);

            if(connect(sockfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in)) < 0){
                perror("connect");
                goto err;
            }
            /*
            置套接字为非阻塞模式的函数
            默认阻塞行为：
                当调用 read()/write()/accept() 等函数时，若操作无法立即完成（如无数据可读、缓冲区满），进程会阻塞（暂停执行）直到操作完成。
            非阻塞行为：
                通过 ntySetNonblock(sockfd) 设置后，上述函数会立即返回，并通过返回值和 errno 指示操作状态：
                EAGAIN 或 EWOULDBLOCK：表示操作需稍后重试（无数据可读/缓冲区满）。
            */
            ntySetNonblock(sockfd);
            /*
            允许端口重用，避免因 TIME_WAIT 状态导致端口绑定失败
                当服务器主动关闭连接时，操作系统会将端口置于 TIME_WAIT 状态（持续 2MSL，通常 1-4 分钟）
                ，导致短时间内无法重新绑定同一端口。通过 SO_REUSEADDR 选项，可立即重用处于 TIME_WAIT 状态的端口。
            */
            ntySetReUseAddr(sockfd);

            sprintf(buffer,"Hello Server: client --> %d\n",connections);
            send(sockfd,buffer,strlen(buffer),0);

            ev.data.fd = sockfd;
            ev.events = EPOLLIN || EPOLLOUT;
            epoll_ctl(epoll_fd,EPOLL_CTL_ADD,sockfd,&ev);

            connections++;
        }

        if(connections % 1000 == 999 || connections >= 34000){
            struct timeval tv_cur;
            memcpy(&tv_cur,&tv_begin,sizeof(struct timeval));

            gettimeofday(&tv_begin,NULL);

            //计算两个时间点之间差值（毫秒）的宏/函数
            int time_used = TIME_SUB_MS(tv_begin,tv_cur);
            printf("connections: %d, sockfd: %d, time_used: %d\n",connections,sockfd,time_used);

            /*
            用于高效等待文件描述符事件
            int epoll_wait(int epfd,              // epoll 实例文件描述符
               struct epoll_event *events, // 存储就绪事件的数组
               int maxevents,         // 数组最大容量
               int timeout);          // 超时时间（毫秒）
            */
            int nfds = epoll_wait(epoll_fd,events,connections,100);
            for(i = 0;i < nfds;i++){
                int clientfd = events[i].data.fd;

                //EPOLLOUT：表示对应的文件描述符（FD）已准备好进行非阻塞写入操作。
                //events[i].events 是一个整型字段，通过位运算存储多个事件标志。
                if(events[i].events & EPOLLOUT){
                    sprintf(buffer,"data from %d\n",clientfd);
                    send(sockfd,buffer,strlen(buffer),0);
                }else if(events[i].events & EPOLLIN){
                    char rBuffer[MAX_BUFFER] = {0};
                    ssize_t length = recv(sockfd,rBuffer,MAX_BUFFER,0);
                    if(length > 0){
                        printf(" RecvBuffer:%s\n",rBuffer);

                        if(!strcmp(rBuffer,"quit")){
                            isContinue = 0;
                        }
                    }else if(length == 0){
                        printf(" Disconnect clientfd:%d\n",clientfd);
                        epoll_ctl(epoll_fd,EPOLL_CTL_DEL,clientfd,NULL);
                        connections--;
                        close(clientfd);
                    }else {
                        if(errno == EINTR) continue;

                        printf(" Error clientfd:%d, errno:%d\n",clientfd,errno);
                        epoll_ctl(epoll_fd,EPOLL_CTL_DEL,clientfd,NULL);
                        close(clientfd);
                    }
                }else{
                    printf(" clientfd:%d, errno:%d\n",clientfd,errno);
                    epoll_ctl(epoll_fd,EPOLL_CTL_DEL,clientfd,NULL);
                    close(clientfd);
                }
            }
        }
        usleep(1 * 1000);
    }

    return 0;

err:
    printf("error : %s\n",strerror(errno));
    return 0;
}
