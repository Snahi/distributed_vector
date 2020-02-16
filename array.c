#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>

// init
#define INIT_VECTOR_QUEUE_NAME   "/init"
#define INIT_VECTOR_QUEUE_MAX_MESSAGES 10
#define MAX_VECTOR_NAME_LEN 40
#define MAX_RESP_QUEUE_NAME_LEN 64

struct init_msg {
    char name[MAX_VECTOR_NAME_LEN];
    int size;
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];
};

#define INIT_MSG_SIZE sizeof(struct init_msg)



int init(char* name, int size)
{
    
}



int main (int argc, char **argv)
{
    mqd_t q_server_init;    // queue to send init vector message to server

    if ((q_server_init = mq_open(INIT_VECTOR_QUEUE_NAME, O_WRONLY)) == -1)
    {
        perror("Cannot open init vector server queue from client perspective");
        exit(1);
    }

    struct init_msg msg;
    msg.size = 10;
    
    while (1)
    {
        if (mq_send(q_server_init, (char*) &msg, INIT_MSG_SIZE, 0) == -1)
        {
            perror("Cannot send init vector message to server");
        }
        else
        {
            printf("message sent\n");
        }
    }

    if (mq_close (q_server_init) == -1) {
        perror ("cannot close q_server_init queue");
        exit (1);
    }

    exit (0);
}