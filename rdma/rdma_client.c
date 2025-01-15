#include "msg_queue.h"
#include "rdma_comm.h"
#include "tools.h"
#include <infiniband/verbs.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>

pthread_t rdma_client_tid;
static struct ibv_wc wc;
static struct ibv_send_wr *bad_wr;
static jia_msg_t *msg_ptr;
int snd_seq[Maxhosts] = {0};
int seq = 0;

void printmsg(jia_msg_t *msg);

int post_send(rdma_connect_t *conn) {
    /* step 1: init wr, sge, for rdma to send */
    struct ibv_sge sge = {.addr = (uint64_t)ctx.out_mr[ctx.outqueue->head]->addr,
                          .length = ctx.out_mr[ctx.outqueue->head]->length,
                          .lkey = ctx.out_mr[ctx.outqueue->head]->lkey};

    struct ibv_send_wr wr = {
        .wr_id = seq,
        .sg_list = &sge,
        .num_sge = 1,
        .next = NULL,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED,};

    /* step 2: loop until ibv_post_send wr successfully */
    jia_msg_t *msg_ptr = (jia_msg_t *)ctx.outqueue->queue[ctx.outqueue->head];
    while (ibv_post_send(ctx.connect_array[msg_ptr->topid].id.qp, &wr, &bad_wr)) {
        log_err("Failed to post send");
    }

    /* step 3: check if we send the packet to fabric */
    while (1) {
        int ne = ibv_poll_cq(ctx.connect_array[msg_ptr->topid].id.send_cq, 1, &wc);
        if (ne < 0) {
            log_err("ibv_poll_cq failed");
            return -1;
        }else if(ne == 0){
            continue;
        }else{
            break;
        }
    }

    /* step 4: check wc.status */
    if (wc.status != IBV_WC_SUCCESS) {
        log_err("Failed status %s (%d) for wr_id %d",
                ibv_wc_status_str(wc.status), wc.status, (int)wc.wr_id);
        return -1;
    }

    return 0;
}

void *rdma_client_thread(void *arg) {
    while (1) {
        /* step 0: get sem value to print */
        int semvalue;
        sem_getvalue(&ctx.outqueue->busy_count, &semvalue);
        log_info(4, "pre client outqueue dequeue busy_count value: %d",
                 semvalue);
        // wait for busy slot
        sem_wait(&ctx.outqueue->busy_count);
        sem_getvalue(&ctx.outqueue->busy_count, &semvalue);
        log_info(4, "enter client outqueue dequeue! busy_count value: conn->%d",
                 semvalue);

        /* step 1: give seqno */
        msg_ptr = (jia_msg_t *)&(ctx.outqueue->queue[ctx.outqueue->head]);
        msg_ptr->seqno = snd_seq[msg_ptr->topid];

        /* step 2: post send mr */
        post_send(&ctx.connect_array[msg_ptr->topid]);

        /* step 3: update snd_seq and head ptr */
        snd_seq[msg_ptr->topid]++;
        ctx.outqueue->head = (ctx.outqueue->head + 1) % SIZE;

        /* step 4: sem post and print value */
        sem_post(&ctx.outqueue->free_count);
        sem_getvalue(&ctx.outqueue->free_count, &semvalue);
        log_info(4, "after client outqueue dequeue free_count value: %d",
                 semvalue);
    }
}

void printmsg(jia_msg_t *msg) {
    printf("msg <from:%d, to:%d, seq:%d, data:%s> \n", msg->frompid, msg->topid,
           msg_ptr->seqno, msg_ptr->data);
}