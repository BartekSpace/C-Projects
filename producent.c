#define _GNU_SOURCE

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>

#define MAX_CLIENTS 512
#define PRODUCTION_BLOCK 640
#define TCP_BLOCK 1664
#define CLIENT_BLOCK 13312
#define POLL_TIMEOUT 1000 //ms
typedef struct transmission{
    struct pollfd fds[MAX_CLIENTS];
    struct sockaddr_in clients_addr[MAX_CLIENTS];
    int clients_recieved[MAX_CLIENTS];

}transmission;

typedef struct data{
    int fd;
    struct sockaddr_in addr;
}data;

typedef struct node
{
    data data;
    struct node* next;
} node_t;

typedef struct queue
{
    node_t* front;
    node_t* back;
    size_t size;
} queue_t;


queue_t* QUEUE_initialize();
size_t QUEUE_size(queue_t* queue);
int QUEUE_enqueue(queue_t* queue, data client_data);
data QUEUE_dequeue(queue_t* queue);
void QUEUE_free(queue_t* queue);

transmission transmission_init();
void remove_disconnected(queue_t* queue);
int connection_verify(int fd);
void register_server(int sockfd, char *Host, in_port_t Port);


int parse(int argc, char* argv[], char addr[], int* port,float* p );
void reproduce(char addr[], int port, float production_speed);
void distribution(int fd_read_pipe,char addr[], int port);
_Noreturn void production(int fd_write, float production_speed);
int create_server_socket(char addr[], int port, struct pollfd *fds);


void arrays_compression( transmission* current_transmission, int *nfds, int *compress_arr);
int timer_init(struct pollfd *fds);

void raport_disconnect(int wasted_bytes, struct sockaddr_in addrr);

void raport_period(int fd_read_pipe, int connected_clients,  int* prev_bytes_storage);


int transmission_acceptance(queue_t *queue, int nfds, int actual_bytes_in_pipe, int reserved, int* connected_clients,struct transmission* current_transmission );

int connect_all_clients(queue_t *queue, int connected_clients, int server_sock_fd);

void disconnect(int i, int *connected_clients, int *reserved, int *compress_arr, transmission *curr_trans);

int main(int argc, char* argv[])
{
    char addr[256]={0};
    float production_speed; //   [B/s]
    int port;

if(parse(argc,argv,addr,&port,&production_speed)==-1) //parsowanie
    exit(EXIT_FAILURE);

reproduce(addr,port,production_speed);
    return 0;
}

_Noreturn void production(int fd_write, float production_speed)
{
    char ascii_character =65;
    char buff[PRODUCTION_BLOCK]={0};
    struct timespec sleeping_time;
    float time = PRODUCTION_BLOCK/production_speed;
    unsigned long seconds = (long)time;
    unsigned long nanoseconds = (long)((time-(float)seconds)*1e9);
    sleeping_time.tv_sec = seconds;
    sleeping_time.tv_nsec = nanoseconds;

    while(1)
    {
        if(ascii_character==123) ascii_character=65;
        if(ascii_character==91) ascii_character=97;
        memset(buff,ascii_character,PRODUCTION_BLOCK*sizeof (char ));
        nanosleep(&sleeping_time,NULL);
        write(fd_write,buff,PRODUCTION_BLOCK);
        ascii_character++;
    }
}
void reproduce(char addr[], int port, float production_speed) {

    int fd[2]; //0 -> read ,  1-> write
    if (pipe(fd)) {
        printf("Pipe error!\n");
        exit(EXIT_FAILURE);
    }
    switch (fork()) {
        case -1:
            printf("Fork error!\n");
            exit(EXIT_FAILURE);
        case 0:

            close(fd[0]); //zamykam koniec do czytania
            production(fd[1], production_speed);

        default:
            close(fd[1]); //zamykam koniec do pisania
            distribution(fd[0], addr, port);
    }
}
int parse(int argc, char* argv[], char addr[], int* port,float* p )
{
    int opt;
    char* pEnd = NULL;

    while((opt=getopt(argc,argv,":p:"))!=-1)
    {
        switch (opt)
        {
            case 'p':
                *p = (float)strtod(optarg,&pEnd);
                *p *= 2662;
                if(*pEnd)
                {
                    printf("-p argument must be a float!\n");
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

    int port_tmp = (int)strtol(port_str,&pEnd,0);
    if(*pEnd)
    {
        printf("Port must be an number!\n");
        return -1;
    }
    *port=port_tmp;
    return 1;
}
void distribution (int fd_read_pipe,char server_addr[], int port)
{
    queue_t* queue = QUEUE_initialize();
    transmission curr_trans = transmission_init();

int connected_clients =0,nfds=2,reserved=0,compress_arr=0,previous_bytes_in_pipe =0,check,current_size,actual_bytes_in_pipe,rec;
    int server_sock_fd = create_server_socket(server_addr, port, curr_trans.fds);
    int time_fd = timer_init(curr_trans.fds);
    char buff[TCP_BLOCK];
    memset(buff,0,TCP_BLOCK);

    while (1) {

        check = ioctl(fd_read_pipe, FIONREAD, &actual_bytes_in_pipe);
        if (check < 0) {
            printf("ioctl() fail\n");
            exit(EXIT_FAILURE);
        }

        check = poll(curr_trans.fds, nfds, POLL_TIMEOUT); //timeout ustawiony po to, żeby uniknac wiecznego zawieszenia na pollu

        if (check < 0) {
            printf("poll() fail\n");
            exit(EXIT_FAILURE);
        }

        if(check>0)
        {
            current_size = nfds;
            for (int i = 0; i < current_size; i++)
            {
                if (curr_trans.fds[i].revents == 0)
                    continue;

             if((curr_trans.fds[i].revents & POLLIN) && curr_trans.fds[i].fd == time_fd) // zdarzenie wygenerowane przez budzik
            {
                read(time_fd,NULL,8);
                raport_period(fd_read_pipe, connected_clients, &previous_bytes_in_pipe);
            }


                if (curr_trans.fds[i].fd == server_sock_fd) // ackeptuj wszystkich czekających klientow
                    connected_clients = connect_all_clients(queue, connected_clients,server_sock_fd);

                if (curr_trans.fds[i].revents & POLLOUT) //do tego ifa wchodzą tylko klienci z otwartą transmisją!
                {
                    if (curr_trans.clients_recieved[i] == 0)
                        reserved += CLIENT_BLOCK;// jesli klient nie pobral jeszcze zadnej paczki to zarezerwuj 13KiB
                    rec = read(fd_read_pipe, buff, TCP_BLOCK);
                    if(connection_verify(curr_trans.fds[i].fd)==-1)
                        disconnect(i, &connected_clients, &reserved, &compress_arr, &curr_trans);

                    else {
                            int sended = send(curr_trans.fds[i].fd, buff, TCP_BLOCK, MSG_NOSIGNAL);
                            if (sended == -1)
                                disconnect(i, &connected_clients, &reserved, &compress_arr, &curr_trans);
                            ////******************UWAGA*******************////
                            /*zaobserwowałem tutaj ciekawe zjawisko: zawuazylem ze metoda weryfikacji połączenia
                            przy uzyciu recv() nie zawsze działa (nigdy sie nie wysypuje jak nie bylo wczesniej wymiany z klientem,
                             ale w przypadku otwartej transmisji czasem nie wykrywa rozlaczenia)
                             Dlatego dodalem sprawdzanie ilości wyslanych bajtów, jesli send zwraca -1
                             to zakladam ze polaczenie zostalo zamkniete
                            */
                            else
                                {
                                reserved -= rec; // jesli jest polaczenie po pobraniu z bufora, zmniejsz ilosc zarezerwowanych
                                curr_trans.clients_recieved[i] += sended;

                                }
                        }
                }

                if (curr_trans.clients_recieved[i] >= CLIENT_BLOCK) //jezeli klient otrzymal juz 13KiB to konczymy transmije
                    disconnect(i, &connected_clients, &reserved, &compress_arr, &curr_trans);

            }
            if (compress_arr) // kompresujemy wszystkie tablice (usuwamy pola rozlaczonych klientow)
                arrays_compression(&curr_trans, &nfds, &compress_arr);
        }
        nfds = transmission_acceptance(queue, nfds, actual_bytes_in_pipe, reserved,&connected_clients,&curr_trans);

        // sprawdz i otworz nowe transmisje
    }
}

void disconnect(int i, int *connected_clients, int *reserved, int *compress_arr, transmission *curr_trans) {
    (*connected_clients)--;
    (*reserved) -= CLIENT_BLOCK - (*curr_trans).clients_recieved[i]; //
    raport_disconnect(CLIENT_BLOCK - (*curr_trans).clients_recieved[i], (*curr_trans).clients_addr[i]);
    shutdown((*curr_trans).fds[i].fd,O_RDWR); //nie sprawdzam zwracanej wartosci bo jesli polaczenie jest normalnie zamykane
                                                // to shutdown sie wykona zwyczajnie, a jesli juz zostalo zerwane przez klienta
                                                //to shutdown zwroci -1, ale nie ma to znaczenia dla dalszego dzialania programu

    close((*curr_trans).fds[i].fd);
    (*curr_trans).fds[i].fd = -1;
    (*compress_arr) = 1;
}
int connect_all_clients(queue_t *queue, int connected_clients, int server_sock_fd) {

    int client_fd;
    data client_data;
    struct sockaddr_in addr;
    do {
        unsigned int sa_len = sizeof(struct sockaddr_in);
        client_fd = accept(server_sock_fd, &addr,&sa_len); //zaakceptuj wszystkich klientow
        if (client_fd < 0) {
            if (errno != EWOULDBLOCK) {
                printf("accept() fail\n");
                exit(EXIT_FAILURE);
            }
            break;
        }
        connected_clients++;
        client_data.fd = client_fd;
        client_data.addr = addr;

        QUEUE_enqueue(queue,client_data); //wrzucam kazdego klienta do kolejki

    } while (client_fd != -1);
    return connected_clients;
}
int transmission_acceptance(queue_t *queue, int nfds, int actual_bytes_in_pipe, int reserved, int* connected_clients,struct transmission* current_transmission ) {// int added_client = 0;
    int clients_ready_for_transsmision = (actual_bytes_in_pipe - reserved) / CLIENT_BLOCK; //tylu klientow max mozna zdjac z kolejki i otworzyc im transmisje
    int check;
    data client_data;
    remove_disconnected(queue); // przed otworzeniem transmisji sprawdzam czy jacyś klienci sie nie rozlaczyli, jesli tak to ich usuwam

    for (int j = 0; j < clients_ready_for_transsmision; j++)
    {
        check = QUEUE_size(queue);
       if(check==0)
           break; // nie ma wiecej klientow czekajacych na transmisje
        client_data = QUEUE_dequeue(queue);
        current_transmission->clients_addr[nfds] = client_data.addr;
        current_transmission->fds[nfds].fd = client_data.fd;
        current_transmission->fds[nfds].events = POLLOUT;
        nfds++;
    }
    *connected_clients = nfds-2 + QUEUE_size(queue);
    return nfds;
}
void arrays_compression( transmission* current_transmission, int *nfds, int *compress_arr) {
    (*compress_arr) = 0;
    for (int i = 0; i < (*nfds); i++)
    {
        if (current_transmission->fds[i].fd == -1)
        {
            for(int j = i; j < (*nfds); j++)
            {
                current_transmission->fds[j].fd = current_transmission->fds[j+1].fd;
                current_transmission->clients_recieved[j] = current_transmission->clients_recieved[j+1];
                current_transmission->clients_addr[j] =  current_transmission->clients_addr[j+1];
            }
            i--;
            (*nfds)--;
        }
    }
}
int connection_verify(int fd)
{
    int test = recv(fd,NULL,1,MSG_PEEK|MSG_DONTWAIT);
    if(test==0)
        return -1;
    return 1;
}

void raport_period(int fd_read_pipe, int connected_clients,  int* prev_bytes_storage) {
    struct timespec tp;
    int bytes_in_storage=0;
    int pipe_size =fcntl(fd_read_pipe,F_GETPIPE_SZ);
    int check = clock_gettime(CLOCK_REALTIME,&tp);
    float storage_percentage;
    if(check<0)
    {
        printf("clock_gettime() fail\n");
        exit(EXIT_FAILURE);
    }

    check = ioctl(fd_read_pipe,FIONREAD,&bytes_in_storage);
    if(check<0)
    {
        printf("ioctl() fail\n");
        exit(EXIT_FAILURE);
    }
    storage_percentage = (float)bytes_in_storage*100/(float)pipe_size;
    fprintf(stderr,"TS: %ld.%ld\n",tp.tv_sec,tp.tv_nsec);
    fprintf(stderr,"Conected Clients: %d\n",connected_clients);
    fprintf(stderr,"Bytes in storage: %d : %.3f%%\n",bytes_in_storage,storage_percentage);
    fprintf(stderr,"Bytes flow: %d\n",bytes_in_storage-*prev_bytes_storage);
    fprintf(stderr,"\n");
    *prev_bytes_storage = bytes_in_storage;
}
void raport_disconnect(int wasted_bytes, struct sockaddr_in addrr){
    struct timespec TS;
    if(clock_gettime(CLOCK_REALTIME,&TS) <0)
    {
        printf("clock_gettime() fail\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr,"TS: %ld.%ld\n",TS.tv_sec,TS.tv_nsec);
    fprintf(stderr,"disconnected client address: %s:%d \n",inet_ntoa(addrr.sin_addr),(int)ntohs(addrr.sin_port));
    fprintf(stderr,"wasted bytes: %d\n",wasted_bytes);
    fprintf(stderr,"\n");
}

int create_server_socket(char addr[], int port, struct pollfd *fds) {
    int server_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    int check, ln =1;

    if( server_sock_fd <0 ) {
       printf("sock() fail\n");
        exit(EXIT_FAILURE);
    }
    check = setsockopt(server_sock_fd,SOL_SOCKET,SO_REUSEADDR,(char*)&ln,sizeof(ln));

    if(check<0)
    {
        printf("setsockopt() fail\n");
        close(server_sock_fd);
        exit(EXIT_FAILURE);
    }

    check = ioctl(server_sock_fd,FIONBIO,(char*)&ln); //set socket to nonblock
    if(check<0)
    {
        printf("ioctl() fail\n");
        exit(EXIT_FAILURE);
    }
    register_server(server_sock_fd, addr, port);

    check = listen(server_sock_fd,32);
    if(check<0)
    {
        printf("listen() fail\n");
        close(server_sock_fd);
        exit(EXIT_FAILURE);
    }

    fds[0].fd = server_sock_fd;
    fds[0].events = POLLIN;
    return server_sock_fd;
}
void register_server(int sockfd, char * Host, in_port_t Port ) {


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

    if( bind(sockfd,(struct sockaddr *)&A,sizeof(A)) )
    {
        printf("bind() fail\n");
        exit(EXIT_FAILURE);
    }
}
transmission transmission_init()
{
    transmission my_trans;
    memset(my_trans.clients_addr,0,sizeof (struct sockaddr_in)*MAX_CLIENTS);
    memset(my_trans.fds,0,sizeof(struct pollfd)*MAX_CLIENTS);
    memset(my_trans.clients_recieved,0,sizeof(int)*MAX_CLIENTS);
    return my_trans;
}
int timer_init(struct pollfd *fds) {
    int time_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if(time_fd<0)
    {
        printf("timerfd_create() fail\n");
        exit(EXIT_FAILURE);
    }
    struct itimerspec itime;
    itime.it_interval.tv_sec=5;
    itime.it_interval.tv_nsec=0;
    itime.it_value.tv_nsec=0;
    itime.it_value.tv_sec=5;
    if(timerfd_settime(time_fd,0,&itime,NULL)<0)
    {
        printf("timerfd_settime fail\n");
        exit(EXIT_FAILURE);
    }
    fds[1].fd = time_fd;
    fds[1].events = POLLIN;
    return time_fd;
}

queue_t *QUEUE_initialize()
{
    queue_t *queue = (queue_t*)calloc(1, sizeof(queue_t));
    if (queue == NULL) return NULL;
    queue->front = NULL;
    queue->back = NULL;
    queue->size = 0;
    return queue;
}
size_t QUEUE_size(queue_t* queue)
{
    return queue->size;
}
int QUEUE_enqueue(queue_t* queue, data client_data)
{
    node_t* node = (node_t*)calloc(1, sizeof(node_t));

    if (node == NULL)
    {
         printf("calloc() fail\n");
        return -1;
    }

    node->data = client_data;
    node->next = NULL;

    if (queue->size == 0)
    {
        queue->front = node;
        queue->back = node;
    }
    else
    {
        queue->back->next = node;
        queue->back = node;
    }

    (queue->size)++;

    return 0;
}
data QUEUE_dequeue(queue_t* queue)
{
   // void* ret = QUEUE_peek(queue, data, size);

 data val = queue->front->data;

    if (queue->front == queue->back)
    {
        free(queue->front);
        queue->front = NULL;
        queue->back = NULL;
    }
    else
    {
        node_t* temp = queue->front;
        queue->front = temp->next;
        free(temp);
    }

    (queue->size)--;

    return val;
}
void QUEUE_free(queue_t* queue)
{
    node_t* current = queue->front;

    while (current != NULL)
    {
        node_t* next = current->next;
        free(current);
        current = next;
    }

    free(queue);
}
void remove_disconnected(queue_t* queue)
{
    data client_data;
    struct node* i_node = queue->front;
    struct node* previous_node = i_node;
    if(QUEUE_size(queue)==0) return;

      do
    {
        while(connection_verify(queue->front->data.fd)==-1)
        {
            if(queue->size>0) {

                client_data =QUEUE_dequeue(queue);
                raport_disconnect(0,client_data.addr);
                previous_node=queue->front;
            }
            else
                return;

            if(queue->size==0) return;
        }
        if(previous_node->next==NULL) return;
        if(connection_verify(previous_node->next->data.fd)==-1)
        {
            struct node* tmp = previous_node->next;
            previous_node->next= tmp->next;
            raport_disconnect(0,tmp->data.addr);
            free(tmp);
            queue->size--;
            if(previous_node->next==NULL) {
                queue->back = previous_node;
                return;
            }
        }
        previous_node = previous_node->next;

    } while(previous_node->next != NULL);

}
