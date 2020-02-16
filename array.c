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

    mqd_t q_resp;   // queue for response from server
    struct mq_attr q_resp_attr;
    q_resp_attr.mq_flags = 0;
    q_resp_attr.mq_maxmsg = 1;
    q_resp_attr.mq_msgsize = sizeof(int);
    q_resp_attr.mq_curmsgs = 0;

    if ((q_resp = mq_open("/initResp", O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR, &q_resp_attr)) == -1)
    {
        perror("Cannot open init vector response queue form client perspective");
        exit(1);
    }

    struct init_msg msg;
    msg.size = 10;
    strcpy(msg.name, "hwdp");
    strcpy(msg.resp_queue_name, "/initResp");
    
    int response = 100;

    while (1)
    {
        if (mq_send(q_server_init, (char*) &msg, INIT_MSG_SIZE, 0) == -1)
        {
            perror("Cannot send init vector message to server");
        }
        else
        {
            printf("message sent\n");
            
            if (mq_receive(q_resp, (char*) &response, sizeof(int), NULL) == -1)
            {
                perror("Cannot receive response message in init vector");
            }
            else
            {
                printf("result: %d\n", response);
            }
            
        }
    }

    if (mq_close (q_server_init) == -1) {
        perror ("cannot close q_server_init queue");
    }

    if (mq_close (q_resp) == -1)
    {
        perror ("Cannot close response queue");
    }

    if (mq_unlink("/initResp") == -1)
    {
        perror("Cannot unlink response queue");
    }

    exit (0);
}