#include "msg_queue.h"
#include "rdma_comm.h"
#include "tools.h"
#include <infiniband/verbs.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdlib.h>

void init_recv_wr(struct ibv_mr **mr, unsigned index);
pthread_t rdma_listen_tid;
static struct ibv_recv_wr *bad_wr = NULL;
static struct ibv_wc wc;
struct ibv_sge sge_list[QueueSize * Maxhosts];
struct ibv_recv_wr wr_list[QueueSize * Maxhosts];
extern int jia_pid;

unsigned queue_size = QueueSize;

#define CQID(cq_ptr) (((void *)cq_ptr - (void *)ctx.connect_array) / sizeof(rdma_connect_t))
#define min(a, b) ((a) < (b) ? (a) : (b))

int check_flags(unsigned cqid) {
    msg_queue_t *inqueue = ctx.connect_array[cqid].inqueue;

    if (atomic_load(&(inqueue->free_value)) >= BatchingSize) {
        log_info(3, "pre inqueue [post]: %d, [free_value]: %d [post_value]: %d", inqueue->post,
                 atomic_load(&inqueue->free_value), atomic_load(&inqueue->post_value));

        /* step 1: new BatchingSize post recv */
        init_recv_wr(ctx.connect_array[cqid].in_mr, inqueue->post + cqid * queue_size);
        while (ibv_post_recv(ctx.connect_array[cqid].id.qp,
                             &wr_list[inqueue->post + cqid * queue_size], &bad_wr)) {
            log_err("Failed to post recv");
        };

        /* step 2: add free_value and sub post_value */
        atomic_fetch_sub(&(inqueue->free_value), BatchingSize);
        atomic_fetch_add(&(inqueue->post_value), BatchingSize);

        /* step 3: update inqueue->post */
        pthread_mutex_lock(&inqueue->post_lock);
        inqueue->post = (inqueue->post + BatchingSize) % QueueSize;
        pthread_mutex_unlock(&inqueue->post_lock);

        log_info(3, "after inqueue [post]: %d, [free_value]: %d [post_value]: %d", inqueue->post,
                 atomic_load(&inqueue->free_value), atomic_load(&inqueue->post_value));
    }
    // while (inqueue->flags[Batchid] == 2) {
    //     /* step 1: init new post recv */
    //     init_recv_wr(ctx.connect_array[cqid].in_mr, inqueue->post + cqid * queue_size);

    //     /* step 2: update flags state */
    //     pthread_mutex_lock(&inqueue->flag_lock);
    //     inqueue->flags[Batchid] = 1;
    //     inqueue->post = (inqueue->post + BatchingSize) % QueueSize;
    //     pthread_mutex_unlock(&inqueue->flag_lock);

    //     /* step 3: update Batchid to test */
    //     Batchid = inqueue->post / BatchingSize;
    // }

    return 0;
}

int post_recv(struct ibv_comp_channel *comp_channel) {
    unsigned cqid;
    msg_queue_t *inqueue;
    struct ibv_cq *cq_ptr = NULL;
    void *context = NULL;
    int ret = -1;
    while (1) {
        /* We wait for the notification on the CQ channel */
        ret = ibv_get_cq_event(comp_channel, /* IO channel where we are expecting the notification
                                              */
                               &cq_ptr,   /* which CQ has an activity. This should be the same as CQ
                                             we created before */
                               &context); /* Associated CQ user context, which we did set */
        if (ret) {
            log_err("Failed to get next CQ event due to %d \n", -errno);
            return -errno;
        }

        /* ensure cqid and inqueue*/
        cqid = CQID(context);
        inqueue = (*(rdma_connect_t *)context).inqueue;

        /* Request for more notifications. */
        ret = ibv_req_notify_cq(cq_ptr, 0);
        if (ret) {
            log_err("Failed to request further notifications %d \n", -errno);
            return -errno;
        }

        /* We got notification. We reap the work completion (WC) element. It is
         * unlikely but a good practice it write the CQ polling code that
         * can handle zero WCs. ibv_poll_cq can return zero. Same logic as
         * MUTEX conditional variables in pthread programming.
         */
        ret = ibv_poll_cq(cq_ptr /* the CQ, we got notification for */,
                          1 /* number of remaining WC elements*/, &wc /* where to store */);
        if (ret < 0) {
            log_err("Failed to poll cq for wc due to %d \n", ret);
            /* ret is errno here */
            return ret;
        } else if (ret == 0) {
            continue;
        }
        log_info(3, "%d WC are completed \n", ret);

        /* Now we check validity and status of I/O work completions */
        if (wc.status != IBV_WC_SUCCESS) {
            log_err("Work completion (WC) has error status: %s", ibv_wc_status_str(wc.status));

            switch (wc.status) {
            case IBV_WC_RNR_RETRY_EXC_ERR:
                // 接收端没有准备好，超过重试次数
                log_err("Remote endpoint not ready, retry exceeded\n");
                break;

            case IBV_WC_RETRY_EXC_ERR:
                // 传输重试超过限制
                log_err("Transport retry count exceeded\n");
                break;

            case IBV_WC_LOC_LEN_ERR:
                // 本地长度错误
                log_err("Local length error\n");
                break;

            case IBV_WC_LOC_QP_OP_ERR:
                // QP操作错误
                log_err("Local QP operation error\n");
                break;

            case IBV_WC_REM_ACCESS_ERR:
                // 远程访问错误
                log_err("Remote access error\n");
                break;

            default:
                log_err("Unhandled error status\n");
                break;
            }
        } else {
            log_info(3, "pre inqueue [tail]: %d, [busy_value]: %d [post_value]: %d", inqueue->tail,
                     atomic_load(&inqueue->busy_value), atomic_load(&inqueue->post_value));

            /* step 1: sub post_value and add busy_value */
            if (atomic_load(&(inqueue->post_value)) <= 0) {
                log_err("post value error <= 0");
            } else {
                atomic_fetch_sub(&(inqueue->post_value), 1);
            }
            atomic_fetch_add(&(inqueue->busy_value), 1);

            /* step 2: update tail */
            pthread_mutex_lock(&inqueue->tail_lock);
            inqueue->tail = (inqueue->tail + 1) % QueueSize;
            pthread_mutex_unlock(&inqueue->tail_lock);

            log_info(3, "after inqueue [tail]: %d, [busy_value]: %d [post_value]: %d",
                     inqueue->tail, atomic_load(&inqueue->busy_value), atomic_load(&inqueue->post_value));
        }

        // check_flags(cqid);

        /* Similar to connection management events, we need to acknowledge CQ
         * events
         */
        ibv_ack_cq_events(cq_ptr, 
		       1 /* we received one event notification. This is not 
		       number of WC elements */);
    }
}

/** init BatchingSize num recv_wr */
void init_recv_wr(struct ibv_mr **mr, unsigned index) {
    unsigned limit = min((BatchingSize), ((index / queue_size) + 1) * queue_size - index);
    /* 在当前这个id对应的wr_list中的序号 */
    unsigned rindex = (index % queue_size);

    /* step 1: init sge_list and wr_list */
    for (int i = 0; i < limit; i++) {
        sge_list[index + i].addr = (uint64_t)mr[rindex + i]->addr;
        sge_list[index + i].length = (uint32_t)mr[rindex + i]->length;
        sge_list[index + i].lkey = mr[rindex + i]->lkey;
        /* now we link to the send work request */
        bzero(&wr_list[index + i], sizeof(struct ibv_recv_wr));
        wr_list[index + i].sg_list = &sge_list[index + i];
        wr_list[index + i].num_sge = 1;
    }

    /* step 2: make wr_list link array */
    int i;
    for (i = 0; i < limit - 1; i++) {
        wr_list[index + i].next = &wr_list[index + i + 1];
    }
    wr_list[index + i].next = NULL;
}

int init_listen_recv() {

    int ret;

    /* step 1: init wr, sge, for rdma to recv */
    for (int j = 0; j < Maxhosts; j++) {
        if (j == jia_pid)
            continue;
        for (int i = 0; i < queue_size; i += BatchingSize) {
            init_recv_wr(ctx.connect_array[j].in_mr, j * queue_size + i);
        }
    }

    /* step 2: post recv wr */
    for (int j = 0; j < Maxhosts; j++) {
        if (j == jia_pid)
            continue;

        msg_queue_t *inqueue = ctx.connect_array[j].inqueue;
        for (int i = 0; i < queue_size; i += BatchingSize) {
            /* step 1: loop until ibv_post_recv wr successfully */
            while (
                (ret = ibv_post_recv(ctx.connect_array[j].id.qp, &wr_list[i + j * queue_size], &bad_wr))) {
                log_err("Failed to post recv");
            }

            /* step 2: add free_value and sub post_value */
            atomic_fetch_sub(&(inqueue->free_value), BatchingSize);
            atomic_fetch_add(&(inqueue->post_value), BatchingSize);

            /* step 3: update inqueue->post */
            pthread_mutex_lock(&ctx.connect_array[j].inqueue->post_lock);
            inqueue->post = (inqueue->post + BatchingSize) % QueueSize;
            pthread_mutex_unlock(&ctx.connect_array[j].inqueue->post_lock);
        }
    }

    return 0;
}

void *rdma_listen_thread(void *arg) {
    post_recv(ctx.recv_comp_channel);

    return NULL;
}