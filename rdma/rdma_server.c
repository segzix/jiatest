#include "msg_queue.h"
#include "rdma_comm.h"
#include <infiniband/verbs.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>

extern pthread_cond_t cond_listen;
pthread_cond_t cond_server = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock_server = PTHREAD_MUTEX_INITIALIZER;
pthread_t rdma_server_tid;
jia_msg_t msg;

extern rdma_connect_t connect_array[Maxhosts];

void msg_handle(jia_msg_t *msg);

void *rdma_server_thread(void *arg) {
    while (1) {
        /* step 1: lock and enter inqueue to check if busy slot number is
         * greater than ctx.batching_num */
        // pthread_mutex_lock(&lock_server);

        for (int i = 0; i < Maxhosts; i = (i + 1) % Maxhosts) {
            rdma_connect_t *tmp_connect;
            msg_queue_t *inqueue;
            if (atomic_load(&(connect_array[i].inqueue->busy_value)) != 0) {
                tmp_connect = &connect_array[i];
                inqueue = tmp_connect->inqueue;
                /* step 1: update head point, busy_value and handle msg */
                inqueue->head = (inqueue->head + 1) % inqueue->size;
                atomic_fetch_sub(&(inqueue->busy_value), 1);
                msg_handle((jia_msg_t *)&(inqueue->queue[inqueue->head]));

                /* step 2: update flags array */
                if(!(inqueue->head % BatchingSize)){
                    pthread_mutex_lock(&inqueue->flag_lock);
                    inqueue->flags[(inqueue->head / BatchingSize)-1] = 1;
                    pthread_mutex_unlock(&inqueue->flag_lock);
                }
            }
        }
    }
}

void msg_handle(jia_msg_t *msg) {
    printf("\nReceived message: %s\n", msg->data);
}