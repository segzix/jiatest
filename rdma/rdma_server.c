#include "msg_queue.h"
#include "rdma_comm.h"
#include "tools.h"
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
extern int jia_pid;

extern rdma_connect_t connect_array[Maxhosts];

void msg_handle(jia_msg_t *msg);

void *rdma_server_thread(void *arg) {
    while (1) {
        /* step 1: lock and enter inqueue to check if busy slot number is
         * greater than ctx.batching_num */
        // pthread_mutex_lock(&lock_server);

        for (int i = 0; i < Maxhosts; i = (i + 1) % Maxhosts) {
            /* get connect and inqueue */
            rdma_connect_t *tmp_connect = &ctx.connect_array[i];
            msg_queue_t *inqueue = tmp_connect->inqueue;

            if (i == jia_pid)
                continue;
            if (atomic_load(&(ctx.connect_array[i].inqueue->busy_value)) > 0) {

                /* step 1: handle msg and update head point, busy_value */
                msg_handle((jia_msg_t *)(inqueue->queue[inqueue->head]));

                /* step 2: sub busy_value and add free_value */
                if (atomic_load(&(inqueue->busy_value)) <= 0) {
                    log_err("busy value error <= 0");
                } else {
                    atomic_fetch_sub(&(inqueue->busy_value), 1);
                }
                atomic_fetch_add(&(inqueue->free_value), 1);

                /* step 2: update head */
                pthread_mutex_lock(&inqueue->head_lock);
                inqueue->head = (inqueue->head + 1) % inqueue->size;
                pthread_mutex_unlock(&inqueue->head_lock);
            }
        }
    }
}

void msg_handle(jia_msg_t *msg) {
    printf("\nReceived message: %s\n", msg->data);
}