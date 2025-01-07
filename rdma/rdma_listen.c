#include "msg_queue.h"
#include "rdma_comm.h"
#include "tools.h"
#include <infiniband/verbs.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdlib.h>

pthread_t rdma_listen_tid;
extern pthread_cond_t cond_server;
pthread_cond_t cond_listen = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock_listen = PTHREAD_MUTEX_INITIALIZER;
static struct ibv_recv_wr *bad_wr = NULL;
static struct ibv_wc wc[8];
struct ibv_sge sge_list[20];
struct ibv_recv_wr wr_list[50];
int ack_send, ack_recv;

int post_recv() {
    /* step 1: init wr, sge, for rdma to recv */
    for (int i = 0; i < ctx.batching_num; i++) {
        sge_list[i].addr = (uint64_t)&ctx.inqueue->queue[ctx.inqueue->tail].msg;
        sge_list[i].length = sizeof(jia_msg_t);
        sge_list[i].lkey = ctx.recv_mr[ctx.inqueue->tail]->lkey;
        wr_list[i].sg_list = &sge_list[i];
        wr_list[i].num_sge = 1;
        wr_list[i].wr_id = ctx.inqueue->tail;   // use index as wr_id
        ctx.inqueue->tail = (ctx.inqueue->tail + 1) % ctx.inqueue->size;
    }
    for (int i = 0; i < ctx.batching_num - 1; i++) {
        wr_list[i].next = &wr_list[i + 1];
    }

    /* step 2: loop until ibv_post_recv wr successfully */
    while (ibv_post_recv(ctx.qp, &wr_list[0], &bad_wr)) {
        log_err("Failed to post recv");
    }

    log_info(3, "Post recv wr successfully");

    if (ibv_req_notify_cq(ctx.recv_cq, 0)) {
        log_err("Couldn't request CQ notification");
    }

    /* step 3: check num_completions until we get ctx.batching_num wr */
    int num_completions = 0;
    while (num_completions < ctx.batching_num) {
        int ret = ibv_get_cq_event(ctx.recv_channel, &ctx.recv_cq, (void **)&ctx.context);
        if (ret) {
            perror("ibv_get_cq_event failed");
            break;
        }

        ibv_ack_cq_events(ctx.recv_cq, 1);

        ret = ibv_req_notify_cq(ctx.recv_cq, 0);
        if (ret) {
            fprintf(stderr, "Failed to request CQ notification\n");
            break;
        } 

        int nc = ibv_poll_cq(ctx.recv_cq, ctx.batching_num - num_completions, wc + num_completions);

        /* step 3.1: manage error */
        if (nc < 0) {
            log_err("ibv_poll_cq failed");
            exit(-1);
        } else if (nc == 0) {
            continue;
        } else {
            // ctx.inqueue->tail = (ctx.inqueue->tail + 1) % ctx.inqueue->size;
            for (int i = 0; i < nc; i++) {
                if (wc[num_completions + i].status != IBV_WC_SUCCESS) {
                    log_err("Failed status %s (%d) for wr_id %d",
                        ibv_wc_status_str(wc[num_completions + i].status), wc[num_completions + i].status, (int)wc[num_completions + i].wr_id);
                } else {
                    log_info(3, "Recv mr consumed successfully");
                    // use wc.wr_id to judge which recv_wr has been consumed successfully and change corresponding queue state
                    ctx.inqueue->queue[(int)wc[num_completions + i].wr_id].state = SLOT_BUSY;
                }
            }
        }

        /* step 3.2: update num_completions */
        num_completions += nc;
    }
    return 0;
}

// TODO: if there is not multithread, lock is not necessary for single listen
// thread

void *rdma_listen(void *arg) {
    while (1) {
        /* step 1: lock and enter inqueue to check if free slot number is
         * greater than ctx.batching_num */
        pthread_mutex_lock(&lock_listen);

        /* step 2: wait until free num is satisfied and then sub free_value */
        if (atomic_load(&(ctx.inqueue->free_value)) < ctx.batching_num) {
            pthread_cond_wait(&cond_listen, &lock_listen);
        }

        /* step 3: ibv_post_recv wr and then update free_value and busy_value */
        post_recv();
        atomic_fetch_sub(&(ctx.inqueue->free_value), ctx.batching_num);
        atomic_fetch_add(&(ctx.inqueue->busy_value), ctx.batching_num);

        /* step 4: cond signal ctx.inqueue's dequeue */
        if (atomic_load(&(ctx.inqueue->busy_value)) > 0) {
            pthread_cond_signal(&cond_server);
        }

        /* step 5: unlock ctx.inqueue */
        pthread_mutex_unlock(&lock_listen);
    }
}