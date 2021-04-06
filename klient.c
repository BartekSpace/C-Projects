#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <netdb.h>

struct report_data {
    double first_diff;
    double second_diff;
    struct sockaddr_in addr;
}reportData;

#define CLIENT_BLOCK 13312
#define ONE_PACKET 1664
struct sockaddr_in register_addr(short Port,  char *Host);
int parse(int argc, char* argv[], char addr[], short* port,float* p, int* c, float* d );

int create_socket();

int  connection(int sock_fd, struct sockaddr_in *A);
double  timespec_diff_long (struct timespec *start, struct timespec *stop);

void get_time(struct timespec *current);
void on_exit_handler(int exitStatus, void *arg);
void raport_every_block(struct timespec connected, struct timespec first_portion, struct timespec disconnected, struct sockaddr_in addr);
int main(int argc, char* argv[])
{

float p,d;
int c, ln=1;
short Port;
char addr[256]={0};

    if(!parse(argc,argv,addr,&Port,&p,&c,&d))
        return -1;

    int sock_fd = create_socket();
    setsockopt(sock_fd,SOL_SOCKET,SO_REUSEADDR,(char*)&ln,sizeof(ln));
    struct sockaddr_in A = register_addr(Port, addr);
    struct sockaddr_in my_addr;
    unsigned int sa_len = sizeof(my_addr);
    char buff[CLIENT_BLOCK];
    int received=0, storage =0;
    struct timespec disconnected,connected,first_portion,begin,sleeping_time;
    float time = ONE_PACKET/p;

    unsigned long seconds = (long)time;
    unsigned long nanoseconds = (long)((time-(float)seconds)*1e9);
    sleeping_time.tv_sec = seconds;
    sleeping_time.tv_nsec = nanoseconds;

    int first_block=1;
    int first_package = 1;
while(1)
{
    get_time(&begin);
    first_package = 1;

    if(c-storage<CLIENT_BLOCK)
        break;
     sock_fd = create_socket();
    if(connection(sock_fd, &A)<0)
    {
        printf("nie udało się zaakceptować połączenia\n");
        return 1;
    }
    get_time(&connected);

    if(getsockname( sock_fd, (struct sockaddr *)&my_addr, &sa_len) <0 )
    {
        printf("getsockname() fail\n");
        exit(EXIT_FAILURE);
    }
    received=0;
    while (received< CLIENT_BLOCK) {

    received += recv(sock_fd, buff + received, ONE_PACKET, 0);
        if(first_package==1) {
            first_package = 0;
            get_time(&first_portion);
        }
        nanosleep(&sleeping_time,NULL);
    }
//printf("%s\n",buff);
    if( shutdown(sock_fd,SHUT_RDWR) ) {
        printf("shutdown() fail\n");
        exit(EXIT_FAILURE);
    }

  get_time(&disconnected);

        if(first_block) { // jezeli jest to pierwszy blok to degradacja bajtow liczona jest od pierwszej porcji
            storage += received - (int) (timespec_diff_long(&first_portion, &disconnected) * d);
            if(storage<0) storage=0;
        }
        else { // w innym wypadku od samego poczatku petli
            storage += received - (int) (timespec_diff_long(&begin, &disconnected) * d);
            if(storage<0) storage=0;
        }

    first_block=0;
    raport_every_block(connected,first_portion,disconnected,my_addr);
}

   close(sock_fd);
struct timespec TS;

if(clock_gettime(CLOCK_REALTIME,&TS) <0)
{
    printf("clock_gettime() fail\n");
    exit(EXIT_FAILURE);
}
    fprintf(stderr,"TS: %ld.%ld\n",TS.tv_sec,TS.tv_nsec);
return 0;

}
void raport_every_block(struct timespec connected, struct timespec first_portion, struct timespec disconnected, struct sockaddr_in addr)
{
    double first_diff = timespec_diff_long(&connected,&first_portion);
    double second_diff = timespec_diff_long(&first_portion,&disconnected);
    struct report_data* report = (struct report_data*)malloc(sizeof (struct report_data));
    report->addr = addr;
    report->first_diff = first_diff;
    report->second_diff =second_diff;
    on_exit(on_exit_handler,report);
}
void get_time(struct timespec *current) {
    int check = clock_gettime(CLOCK_MONOTONIC, current);
    if(check<0)
    {
        printf("clock_gettime() fail\n");
        exit(EXIT_FAILURE);
    }
}
int connection(int sock_fd, struct sockaddr_in *A) {
    int proba = 11;
    while( --proba ) {
        if(connect(sock_fd, (struct sockaddr *) A, sizeof((*A))) != -1 ) break;
    }
    if( ! proba )
        return -1;

    return 1;
}
int create_socket() {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if( sock_fd == -1 ) {
        printf("socket() error\n");
        exit(EXIT_FAILURE);
    }
    return sock_fd;
}
int parse(int argc, char* argv[], char addr[], short* port,float* p, int* c, float* d )
{
    int opt;
    char* pEnd = NULL;
    while((opt=getopt(argc,argv,":p:c:d:"))!=-1)
    {
        switch (opt)
        {
            case 'p':
                *p = (float)strtod(optarg,&pEnd);
                *p *= 4435;
                if(*pEnd)
                {
                    printf("-p argument must be a float!\n");
                    return -1;
                }
                break;

            case 'd':
                *d = (float)strtod(optarg,&pEnd);
                *d *= 819;
                if(*pEnd)
                {
                    printf("-d argument must be a float!\n");
                    return -1;
                }
                break;
            case 'c':
                *c = (int)strtol(optarg,&pEnd,0);
                *c *= 30*1024;
                if(*pEnd)
                {
                    printf("-c argument must be a int!\n");
                    return -1;
                }
                break;


            default:

                printf("Wrong argument!\n");
                return -1;
        }
    }
    if (argc - optind > 1) {printf("Use no more than 1 positional argument!\n"); return -1;}
    if (argc - optind == 0)
    {
        printf("Need port number!\n");
        return -1;
    }
    char* full_addr = argv[optind];
    char* addr_temp = strtok( full_addr, ":" );
    char* port_str = strtok(NULL,":");

    if(port_str==NULL)
    {
        port_str = addr_temp;
        memcpy(addr,"localhost\0",9);

    }

    else
        memcpy(addr,addr_temp,256);

    short port_tmp = (short)strtol(port_str,&pEnd,0);
    if(*pEnd)
    {
        printf("Port must be an number!\n");
        return -1;
    }

    *port=port_tmp;
    return 1;

}
struct sockaddr_in register_addr(short Port,  char *Host) {
    struct sockaddr_in A;
    A.sin_family = AF_INET;
    A.sin_port = htons(Port);

    struct hostent* mapped_addr;
    mapped_addr= gethostbyname(Host);
    if(mapped_addr==NULL)
    {
        printf("Wrong adress\n");
        exit(EXIT_FAILURE);
    }
    A.sin_addr = *(struct in_addr *)(mapped_addr->h_addr);

    return A;
}
double  timespec_diff_long (struct timespec *start, struct timespec *stop)
{
    struct timespec result;
    double time;
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result.tv_sec = stop->tv_sec - start->tv_sec - 1;
        result.tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result.tv_sec = stop->tv_sec - start->tv_sec;
        result.tv_nsec = stop->tv_nsec - start->tv_nsec;
    }

time = result.tv_sec + result.tv_nsec/1e9;
    return time;
}
void on_exit_handler(int exitStatus, void *arg)
{
    char myIP[16];
    unsigned int myPort;
    struct report_data* report = arg;
    inet_ntop(AF_INET, &report->addr.sin_addr, myIP, sizeof(myIP));
    myPort = ntohs(report->addr.sin_port);
    fprintf(stderr,"Client PID: %d and address: %s:%d \n",getpid(),myIP,myPort);
    fprintf(stderr,"Connection->First Package Delay: %lf s\n",report->first_diff);
    fprintf(stderr,"First Package->Disconnection Delay: %lf s\n",report->second_diff);
    fprintf(stderr,"\n");
    free(report);
    exit(exitStatus);
}
