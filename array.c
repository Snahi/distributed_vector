#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <regex.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>



///////////////////////////////////////////////////////////////////////////////////////////////////
// const
///////////////////////////////////////////////////////////////////////////////////////////////////
#define NAME_REGEX "^[a-zA-Z0-9]+$"

// init
#define INIT_VECTOR_QUEUE_NAME "/init"
#define INIT_VECTOR_QUEUE_MAX_MESSAGES 10
#define MAX_VECTOR_NAME_LEN 39
#define MAX_RESP_QUEUE_NAME_LEN 64
#define NEW_VECTOR_CREATED 1
#define VECTOR_ALREADY_EXISTS 0
#define VECTOR_CREATION_ERROR -1

struct init_msg {
    char name[MAX_VECTOR_NAME_LEN];
    int size;
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];
};

#define INIT_MSG_SIZE sizeof(struct init_msg)



///////////////////////////////////////////////////////////////////////////////////////////////////
// function declarations
///////////////////////////////////////////////////////////////////////////////////////////////////



int is_init_data_valid(char* name, int size);
int is_name_valid(char* name);
int open_server_init_queue(mqd_t* p_queue);
/*
    creates and opens unique queue for getting integer response from server
*/
int open_resp_queue(char* que_name, mqd_t* p_queue);
int create_vector_on_server(char* name, int size, char* resp_que_name, mqd_t* p_q_server, 
    mqd_t* p_q_resp);



///////////////////////////////////////////////////////////////////////////////////////////////////
// init
///////////////////////////////////////////////////////////////////////////////////////////////////



int init(char* name, int size)
{
    int result = NEW_VECTOR_CREATED;

    if (is_init_data_valid(name, size))
    {
        // open queue to send init vector message to server
        mqd_t q_server_init;

        if ((q_server_init = mq_open(INIT_VECTOR_QUEUE_NAME, O_WRONLY)) == -1)
        {
            perror("Cannot open init vector server queue from client perspective");
            result = VECTOR_CREATION_ERROR;
        }
        else
        {
            // queue for response from server
            mqd_t q_resp;
            char resp_que_name[MAX_RESP_QUEUE_NAME_LEN];
            if (open_resp_queue(resp_que_name, &q_resp) == 1)
            {
                result = create_vector_on_server(name, size, resp_que_name, &q_server_init, &q_resp);

                // close and delete response queue
                if (mq_close (q_resp) == -1)
                    perror ("Cannot close response queue");

                if (mq_unlink(resp_que_name) == -1)
                    perror("Cannot unlink response queue");
            }
            else
            {
                perror("RESPONSE QUEUE couldn't open response queue");
                result = VECTOR_CREATION_ERROR;
            }

            if (mq_close (q_server_init) == -1) 
            {
                perror ("cannot close q_server_init queue");
            }
        }
    }
    else 
        result = VECTOR_CREATION_ERROR;
    
    return result;
}



int is_init_data_valid(char* name, int size)
{
    int is_size_val = size > 0;
    int is_name_val = is_name_valid(name);

    return is_size_val && is_name_val;
}



int is_name_valid(char* name)
{
    int res = 1;
    size_t len = strlen(name);

    if (len >= 1 && len <= MAX_VECTOR_NAME_LEN)
    {
        regex_t regex;
        if (regcomp(&regex, NAME_REGEX, REG_EXTENDED | REG_NOSUB) == 0)
        {
            if (regexec(&regex, name, 0, NULL, 0) == 0)
            {
                res = 1;
            }
            else
            {
                res = 0;
            }
            regfree(&regex);
        }
        else
        {
            res = 0;
            printf("Could not compile regex\n");
        }
    }
    else
    {
        res = 0;
    }
    
    return res;
}



int open_resp_queue(char* que_name, mqd_t* p_queue)
{
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 1;
    attr.mq_msgsize = sizeof(int);
    attr.mq_curmsgs = 0;

    char local_que_name[MAX_RESP_QUEUE_NAME_LEN];

    // make queue name with PID, in single thread client it will be unique
    snprintf(local_que_name, MAX_RESP_QUEUE_NAME_LEN, "/initvector%d", getpid());
    // flags which assure that queue with such name doesn't exist
    int flags = O_CREAT | O_EXCL | O_RDONLY;
    // will be used to generate unique name in case it's needed
    char* random_str = "1";
    // if name is not unique, add random number to it. If there is no more space
    // start from the beginning
    while((*p_queue = mq_open(local_que_name, flags, S_IRUSR | S_IWUSR, &attr)) == -1)
    {
        if (errno == EEXIST)
        {
            // reached maximum name len, reset the name
            if (strlen(local_que_name) == MAX_RESP_QUEUE_NAME_LEN - 1)
            {
                snprintf(local_que_name, MAX_RESP_QUEUE_NAME_LEN, "initvector%d", getpid());
            }
            else
            {
                // generate random number and convert it to string
                snprintf(random_str, 2, "%d", rand() % 10);
                // append generated number to the name
                strcat(local_que_name, random_str);
            }
        }
        else // error occurred (but not because of non unique name)
        {
            return 0;
        }
    }

    strcpy(que_name, local_que_name);
    
    return 1;
}



int create_vector_on_server(char* name, int size, char* resp_que_name, mqd_t* p_q_server, 
    mqd_t* p_q_resp)
{
    int result = NEW_VECTOR_CREATED;

    // create message
    struct init_msg msg;
    msg.size = size;
    strcpy(msg.name, name);
    strcpy(msg.resp_queue_name, resp_que_name);

    // send message
    if (mq_send(*p_q_server, (char*) &msg, INIT_MSG_SIZE, 0) == -1)
    {
        perror("Cannot send init vector message to server");
        result = VECTOR_CREATION_ERROR;
    }
    else // message send successfully
    {
        // wait for response
        if (mq_receive(*p_q_resp, (char*) &result, sizeof(int), NULL) == -1)
        {
            perror("Cannot receive response message in init vector");
            result = VECTOR_CREATION_ERROR;
        }
    }

    return result;
}



int main (int argc, char **argv)
{
    int res = init("c", 12);
    printf("%d\n", res);

    exit (0);
}