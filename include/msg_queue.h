#ifndef MSG_QUEUE_H
#define MSG_QUEUE_H
#include <pthread.h>
#include <semaphore.h>

#define Maxmsgsize 40928

typedef enum {
    DIFF,
    DIFFGRANT,
    GETP,
    GETPGRANT,
    ACQ,
    ACQGRANT,
    INVLD,
    BARR,
    BARRGRANT,
    REL,
    WTNT,
    JIAEXIT,
    WAIT,
    WAITGRANT,
    STAT,
    STATGRANT,
    ERRMSG,
    SETCV,
    RESETCV,
    WAITCV,
    CVGRANT,
    MSGBODY,
    MSGTAIL,
    LOADREQ,
    LOADGRANT,
    BCAST = 100,
} msg_op_t;

typedef struct jia_msg {
    msg_op_t op;          /* msg operation type */
    unsigned int frompid; /* from pid */
    unsigned int topid;   /* to pid */
    unsigned int temp;    /* Useless (flag to indicate read or write request)*/
    unsigned int seqno;   /* sequence number */
    unsigned int index;   /* msg index in msg array */
    unsigned int scope;   /* Inca. no.  used as tag in msg. passing */
    unsigned int size;    /* data size */
    /* header is 32 bytes */
    unsigned char data[Maxmsgsize];
} jia_msg_t;

typedef struct msg_queue {
    unsigned char **queue; // msg queue
    int size;              // size of queue(must be power of 2)

    pthread_mutex_t head_lock; // lock for head
    pthread_mutex_t tail_lock; // lock for tail
    pthread_mutex_t post_lock; // lock for post
    volatile unsigned head;    // head
    volatile unsigned tail;    // tail
    volatile unsigned post;    // post recv point

    sem_t busy_count; // busy slot count
    sem_t free_count; // free slot count

    _Atomic volatile unsigned busy_value;
    _Atomic volatile unsigned free_value;
    _Atomic volatile unsigned post_value;

    // int flags[4];
    // pthread_mutex_t flag_lock;
} msg_queue_t;

extern msg_queue_t outqueue;

/**
 * @brief init_msg_queue - initialize msg queue with specified size
 *
 * @param queue msg queue
 * @param size if size = 0, use default size (i.e. system_setting.msg_queue_size)
 * @return int 0 if success, -1 if failed
 */
int init_msg_queue(msg_queue_t *queue, int size);

/**
 * @brief enqueue - enqueue msg
 *
 * @param queue msg queue
 * @param msg msg
 * @return int 0 if success, -1 if failed
 */
int enqueue(msg_queue_t *queue, jia_msg_t *msg);

/**
 * @brief dequeue - dequeue msg
 *
 * @param queue msg queue
 * @param msg msg
 * @return 0 if success, -1 if failed
 */
int dequeue(msg_queue_t *queue, jia_msg_t *msg);

/**
 * @brief free_msg_queue -- free the resources allocated for msg_queue
 *
 * @param msg_queue
 */
void free_msg_queue(msg_queue_t *msg_queue);
#endif