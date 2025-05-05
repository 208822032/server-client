#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <errno.h>
#include <fcntl.h>

#include <sys/epoll.h>

#define BUFFER_LENGTH      1024
#define EPOLL_SIZE         1024

#define MAX_PORT           100

void* client_routine(void* arg){

    int client_fd = *(int *)arg;

    //接受信息
    while(1){
        char buffer[BUFFER_LENGTH] = {0};
        //非阻塞方法 若没收到信息 则一直挂起
        /*
        sockfd	int	已连接的 Socket 文件描述符（由 accept() 返回或 connect() 成功的 Socket）
        buf	void*	接收数据的缓冲区指针
        len	size_t	缓冲区的最大容量（字节数）
        flags	int	控制接收行为的标志位（通常设为0）
            0	默认行为，阻塞接收
            MSG_DONTWAIT	非阻塞接收，没有数据时立即返回
            MSG_PEEK	查看数据但不从接收队列移除
            MSG_WAITALL	等待直到请求的字节数全部接收
        */
        int len = recv(client_fd,buffer,BUFFER_LENGTH,0);
        if(len < 0){
            close(client_fd);
            break;
        }else if(len == 0){
            close(client_fd);
            break;
        }else{
            printf("Recv: %s, %d byte(s)\n",buffer,len);
        }
    }

}

int islistenfd(int fd,int* fds){
    
    int i = 0;
    for(i = 0;i < MAX_PORT;i++){
        if(fd == *(fds+i)) return fd;
    }
    return 0;
}


//一请求一线程
//
int main(int argc, char const *argv[])
{
   if(argc < 2) return -1;

   //由命令行输入端口号
   //atoi C 标准库（<stdlib.h>）提供的函数，用于将字符串转换为整数
   //它会跳过字符串开头的空白字符（如空格、制表符等），然后解析数字字符，直到遇到非数字字符为止
   //argv[1]
   int port = atoi(argv[1]);
   int sockfds[MAX_PORT] = {0};//存放所有在监听的socket
   int epfd = epoll_create(1);

   /*
    AF_INET 表示使用 IPv4 地址族（AF_INET6 是 IPv6）。
    SOCK_STREAM 表示 面向连接的流式 Socket（TCP 协议）。
        如果使用 UDP，应改为 SOCK_DGRAM（数据报模式）。
    0 表示自动选择协议（TCP 对应 IPPROTO_TCP，UDP 对应 IPPROTO_UDP）。
   
   */
  int i = 0;
  for(i = 0;i < MAX_PORT;i++){
  
    int sockfd = socket(AF_INET,SOCK_STREAM,0);

    //socket需要绑定的地址
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;//使用的协议族
    addr.sin_port = htons(port+i);
    addr.sin_addr.s_addr = INADDR_ANY;//令地址为 0.0.0.0

    //将socket与地址绑定
    //(struct sockaddr*)&add 为了兼容不同的地址结构体
    //int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    /*
    bind() 的第二个参数是 struct sockaddr*，它是一个 通用套接字地址结构。
        但实际使用时，我们通常用 struct sockaddr_in（IPv4）或 struct sockaddr_in6（IPv6），
            它们的内存布局和 sockaddr 兼容，但字段不同。
    */
    if(bind(sockfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in)) < 0){
        perror("bind");
        return 2;
    }

    //绑定成功之后开始监听
    //backlog 表示 内核为 sockfd 维护的等待连接队列的最大长度
    if(listen(sockfd,5) < 0){
        perror("listen");
        return 3;
    }

    printf("tcp server listen on port : %d\n",port+i);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    epoll_ctl(epfd,EPOLL_CTL_ADD,sockfd,&ev);

    sockfds[i] = sockfd;
        
  }

#if 0
   //服务器端持续监听 并为每一个请求的用户分配线程
   while(1){

        //使用accept 将被监听到的请求转接到程序处理 生成client_addr
        //接受客户端连接 的关键函数
            //它从已完成连接队列中取出一个连接请求，并为该连接创建一个新的 Socket。
        struct sockaddr_in client_addr;
        memset(&client_addr,0,sizeof(struct sockaddr_in));
        socklen_t client_len = sizeof(client_addr);
        int clientfd = accept(sockfd,(struct sockaddr*)&client_addr,&client_len);

        //创建线程处理该请求
        pthread_t threadid;
        pthread_create(&threadid,NULL,client_routine,&clientfd);
   }
#else
   //创建一个 epoll 实例，返回一个文件描述符 (epfd)，
    //用于后续的 epoll_ctl（管理监听列表）和 epoll_wait（等待事件
    //int epfd = epoll_create(1);
    /*
    struct epoll_event {
        uint32_t     events;    // 监听的事件类型（如 EPOLLIN、EPOLLOUT）
        epoll_data_t data;      // 用户数据（通常关联文件描述符）
    };
    typedef union epoll_data {
        void    *ptr;         // 自定义指针（可用于关联上下文）
        int      fd;          // 关联的文件描述符（最常用）
        uint32_t u32;
        uint64_t u64;
    } epoll_data_t;
    */
    //用于存储 epoll_wait() 返回的就绪事件
    struct epoll_event events[EPOLL_SIZE] = {0};


    //将监听的fd交给epoll管理
    // struct epoll_event ev;//用于处理已有的io listen
    // ev.events = EPOLLIN;//只关注输入
    // ev.data.fd = sockfd;

    //int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
    //向 epoll 实例注册、修改或删除文件描述符（fd）及其关注的事件类型
    /*
    epfd
        epoll 实例的文件描述符，通过 epoll_create() 或 epoll_create1() 创建。
    op
        操作类型，取值为：
        EPOLL_CTL_ADD：将 fd 添加到 epoll 实例，并设置关注的事件。
        EPOLL_CTL_MOD：修改已注册的 fd 的关注事件。
        EPOLL_CTL_DEL：从 epoll 实例中移除 fd（此时 event 参数可忽略或设为 NULL）。
    fd
        需要操作的文件描述符（如 socket、管道等）。
    event
        指向 struct epoll_event 的指针，定义关注的事件类型和用户数据：
    */
    //epoll_ctl(epfd,EPOLL_CTL_ADD,sockfd,&ev);
    

    while(1){
        
        //int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
        /*
        epfd
            epoll 实例的文件描述符（通过 epoll_create() 或 epoll_create1() 创建）。
        events
            输出参数，指向 struct epoll_event 数组的指针，用于存储就绪事件的信息。
        maxevents
            指定 events 数组的最大长度（即单次调用最多返回的事件数）。
        timeout
            超时时间（毫秒）：
            -1：永久阻塞，直到有事件就绪。
            0：立即返回，非阻塞模式。
            >0：阻塞指定时间后返回。
        */
       //返回就绪事件的数量
       //重点就在于epoll_wait
        int nready = epoll_wait(epfd,events,EPOLL_SIZE,5);
        if(nready == -1) continue;

        int i = 0;
        //依次处理每个事件
        for(i = 0;i < nready;i++){

            int sockfd = islistenfd(events[i].data.fd,sockfds);
            if(sockfd){//检查当前事件是否由sockfd触发 listen_fd client_fd

                struct sockaddr_in client_addr;
                memset(&client_addr,0,sizeof(struct sockaddr_in));
                socklen_t client_len = sizeof(client_addr);

                int clientfd = accept(sockfd,(struct sockaddr*)&client_addr,&client_len);

                fcntl(clientfd,F_SETFL,O_NONBLOCK);

                int reuse = 1;
                setsockopt(clientfd,SOL_SOCKET,SO_REUSEADDR,(char*)&reuse,sizeof(reuse));

                //EPOLLET 边缘触发模式 一次性将数据读完
                //EPOLLLT 水平输出模式 一直在接受数据
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = clientfd;
                epoll_ctl(epfd,EPOLL_CTL_ADD,clientfd,&ev);
            }else{//其他连接的操作

                int clientfd = events[i].data.fd;

                char buffer[BUFFER_LENGTH] = {0};

                int len = recv(clientfd,buffer,BUFFER_LENGTH,0);
                if(len < 0){
                    close(clientfd);
                    
                    struct epoll_event ev;
                    ev.events = EPOLLIN;
                    ev.data.fd = clientfd;
                    epoll_ctl(epfd,EPOLL_CTL_DEL,clientfd,&ev);
                }else if(len == 0){
                    close(clientfd);

                    struct epoll_event ev;
                    ev.events = EPOLLIN;
                    ev.data.fd = clientfd;
                    epoll_ctl(epfd,EPOLL_CTL_DEL,clientfd,&ev);
                }else{
                    printf("Recv: %s, %d byte(s)\n",buffer,len);
                }
            }

            
        }
    }

#endif
   return 0;
}
