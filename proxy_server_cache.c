#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int PORT = 8080;
const int MAX_CLIENTS = 15;
const int MAX_BYTES = 8192;

typedef struct cache_element
{
    char *data;
    int len;
    char *url;
    time_t lru_time_track;
    struct cache_element *next;
} cache_element;

cache_element *find_cache_element(char *url);
int add_cache_element(char *data, int size, char url);
void remove_cache_element();

int proxy_socketId;
pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

cache_element *head = NULL;
int cache_size;

void *thread_fn(void *socketNew)
{
    sem_wait(&semaphore);
    int semaphore_value;
    sem_getvalue(&semaphore, &semaphore_value);
    printf("Value of Semaphore:- %d\n", semaphore_value);
    int *t = (int *)socketNew;
    int socket = *t;
    int bytes_send_client, len;

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);

    while (bytes_send_client > 0)
    {
        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else
        {
            break;
        }
    }
    char *tempReq = (char *)calloc(strlen(buffer) + 1, sizeof(char));
    for (size_t i = 0; i < strlen(buffer); i++)
    {
        tempReq[i] = buffer[i];
    }
    cache_element *temp = find(tempReq);
    if (temp != NULL)
    {
        int size = temp->len / sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while (pos < size)
        {
            bzero(response, MAX_BYTES);
            for (size_t i = 0; i < MAX_BYTES; i++)
            {
                response[i] = temp->data[i];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
            printf("Data retreived from the cache\n");
            printf("%s\n\n", response);
        }
    }
    else if (bytes_send_client > 0)
    {
        len = strlen(buffer);
        struct ParsedRequest *request = ParsedRequest_create();
        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            perror("Parsing Failed\n");
        }
        else
        {
            if (!strcmp(request->method, "GET"))
            {
                if (request->host && request->path && checkHTTPversion(request->version) == 1)
                {
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if (bytes_send_client < 0)
                    {
                        sendErrorMessage(socket, 500);
                    }
                }
                else
                {
                    sendErroMessage(socket, 500);
                }
            }
            else
            {
                printf("The Server can only handle GET method\n");
            }
        }
        ParsedRequest_destroy(request);
    }
    else if (bytes_send_client == 0)
    {
        printf("Client is disconnected\n");
    }
    else
    {
        printf("Error in recieving from Client\n");
    }
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);

    sem_getvalue(&semaphore, &semaphore_value);
    printf("Value of Semaphore:- %d\n", semaphore_value);
    free(tempReq);
    return NULL;
}

int main(int argc, char const *argv[])
{
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&semaphore, NULL);

    if (argc == 2)
    {
        PORT = atoi(argv[1]);
    }
    printf("Starting Proxy Server at port: %d\n", PORT);

    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId < 0)
    {
        perror("Failed to create a Socket");
        exit(1);
    }
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse) < 0))
    {
        perror("setSockOpt FAILED!!");
    }

    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Port is not available");
        exit(1)
    }
    printf("Binding on Port %d\n", PORT);
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if (listen_status < 0)
    {
        perror("Error Listening");
        exit(1);
    }
    int socketId_count = 0;
    int Connected_socketId[MAX_CLIENTS];

    while (1)
    {
        bzero((char *)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len);
        if (client_socketId < 0)
        {
            perror("Not able to Connect to Client");
            exit(1);
        }
        else
        {
            Connected_socketId[socketId_count] = client_socketId;
        }

        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client Successfully connected with port number %d and ip address %s\n", ntohs(client_addr.sin_port), str);

        pthread_create(&tid[socketId_count], NULL, thread_fn, (void *)&Connected_socketId[client_socketId]);
        socketId_count++;
    }
    close(proxy_socketId);
    return 0;
}
