#include "msg_queue.h"
#include "rdma_comm.h"
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <infiniband/verbs.h>

extern pthread_cond_t cond_listen;
pthread_cond_t cond_server = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock_server = PTHREAD_MUTEX_INITIALIZER;
pthread_t rdma_server_tid;
jia_msg_t msg;

void msg_handle(jia_msg_t *msg);

void *rdma_server(void *arg) {
    while (1) {
        /* step 1: lock and enter inqueue to check if busy slot number is
         * greater than ctx.batching_num */
        pthread_mutex_lock(&lock_server);

        /* step 2: wait until busy num is satisfied */
        if (atomic_load(&(ctx.inqueue->busy_value)) == 0) {
            pthread_cond_wait(&cond_server, &lock_server);
        }

        /* step 3: handle msg and then sub busy_value */
        if (ctx.inqueue->queue[ctx.inqueue->head].state == SLOT_BUSY) {
            msg_handle(&(ctx.inqueue->queue[ctx.inqueue->head].msg));
        }
        ctx.inqueue->head = (ctx.inqueue->head + 1) % ctx.inqueue->size;
        atomic_fetch_sub(&(ctx.inqueue->busy_value), 1);
        atomic_fetch_add(&(ctx.inqueue->free_value), 1);

        /* step 4: cond signal ctx.inqueue's enqueue */
        if (atomic_load(&(ctx.inqueue->free_value)) >= ctx.batching_num) {
            pthread_cond_signal(&cond_listen);
        }

        /* step 5: unlock ctx.inqueue */
        pthread_mutex_unlock(&lock_server);
    }
}

void msg_handle(jia_msg_t *msg) {
    printf("\nReceived message: %s\n", msg->data);
}