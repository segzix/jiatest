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
extern struct rdma_cm_id rdma_id[Maxhosts];
static struct ibv_recv_wr *bad_wr = NULL;
static struct ibv_wc wc;
struct ibv_sge sge_list[QueueSize * Maxhosts];
struct ibv_recv_wr wr_list[QueueSize * Maxhosts];
extern int jia_pid;

unsigned queue_size = QueueSize;

#define CQID(cq_ptr)                                                           \
    (((void *)cq_ptr - (void *)ctx.connect_array) / sizeof(rdma_connect_t));
#define min(a, b) ((a) < (b) ? (a) : (b))

static int check_flags(unsigned cqid) {
    msg_queue_t *inqueue = ctx.connect_array[cqid].inqueue;

    /* loop until inqueue->flags[Batchid].state != 2(post recv all) */
    unsigned Batchid = inqueue->post / BatchingSize;
    while (inqueue->flags[Batchid] == 2) {
        /* step 1: init new post recv */
        init_recv_wr(ctx.connect_array[cqid].in_mr,
                     inqueue->post + cqid * queue_size);

        /* step 2: update flags state */
        pthread_mutex_lock(&inqueue->flag_lock);
        inqueue->flags[Batchid] = 1;
        inqueue->post =
            (inqueue->post + BatchingSize) % QueueSize;
        pthread_mutex_unlock(&inqueue->flag_lock);

        /* step 3: update Batchid to test */
        Batchid = inqueue->post / BatchingSize;
    }

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
        ret = ibv_get_cq_event(
            comp_channel, /* IO channel where we are expecting the notification
                           */
            &cq_ptr, /* which CQ has an activity. This should be the same as CQ
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
                          1 /* number of remaining WC elements*/,
                          &wc /* where to store */);
        if (ret < 0) {
            log_err("Failed to poll cq for wc due to %d \n", ret);
            /* ret is errno here */
            return ret;
        }else if(ret == 0){
            continue;
        }
        log_info(3, "%d WC are completed \n", ret);

        /* Now we check validity and status of I/O work completions */
        if (wc.status != IBV_WC_SUCCESS) {
            log_err("Work completion (WC) has error status: %s",
                    ibv_wc_status_str(wc.status));

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
            /* step 1: update tail */
            pthread_mutex_lock(&inqueue->tail_lock);
            inqueue->tail = (inqueue->tail + 1) % QueueSize;
            pthread_mutex_unlock(&inqueue->tail_lock);

            /* step 2: add busy_value to let server thread in */
            atomic_fetch_add(&(inqueue->busy_value), 1);
        }

        check_flags(cqid);

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
    unsigned limit =
        min((BatchingSize), ((index / queue_size) + 1) * queue_size - index);
    /* 在当前这个id对应的wr_list中的序号 */
    unsigned rindex = (index % queue_size);

    for (int i = 0; i < limit; i++) {
        sge_list[index + i].addr = (uint64_t)mr[rindex + i]->addr;
        sge_list[index + i].length = (uint32_t)mr[rindex + i]->length;
        sge_list[index + i].lkey = mr[rindex + i]->lkey;
        /* now we link to the send work request */
        bzero(&wr_list[index + i], sizeof(struct ibv_recv_wr));
        wr_list[index + i].sg_list = &sge_list[index + i];
        wr_list[index + i].num_sge = 1;
    }

    int i;
    for (i = 0; i < limit-1; i++) {
        wr_list[index + i].next = &wr_list[index + i + 1];
    }
    wr_list[index + i].next = NULL;
}

int init_listen_recv() {
    /* step 1: init wr, sge, for rdma to recv */
    for (int j = 0; j < Maxhosts; j++) {
        if (j == jia_pid)
            continue;
        for (int i = 0; i < queue_size; i += BatchingSize) {
            // sge_list[i].addr =
            // (uint64_t)&ctx.inqueue->queue[ctx.inqueue->tail].msg;
            // sge_list[i].length = sizeof(jia_msg_t);
            // sge_list[i].lkey = ctx.recv_mr[ctx.inqueue->tail]->lkey;
            // wr_list[i].sg_list = &sge_list[i];
            // wr_list[i].num_sge = 1;
            // wr_list[i].wr_id = ctx.inqueue->tail;   // use index as wr_id
            init_recv_wr(ctx.connect_array[j].in_mr, j * queue_size + i);
            ctx.connect_array[j].inqueue->tail =
                (ctx.connect_array[j].inqueue->tail + 1) %
                ctx.connect_array[j].inqueue->size;
        }
    }

    /* step 2: loop until ibv_post_recv wr successfully */
    for (int j = 0; j < Maxhosts; j++) {
        if (j == jia_pid)
            continue;
        for (int i = 0; i < queue_size; i += BatchingSize) {
            while (ibv_post_recv(ctx.connect_array[j].id.qp,
                                 &wr_list[i + j * queue_size], &bad_wr)) {
                log_err("Failed to post recv");
            }

            /** update flags from 0 to 1 */
            pthread_mutex_lock(&ctx.connect_array[j].inqueue->flag_lock);
            ctx.connect_array[j].inqueue->flags[i / BatchingSize] = 1;
            pthread_mutex_unlock(&ctx.connect_array[j].inqueue->flag_lock);
        }
    }

    return 0;
}

void *rdma_listen_thread(void *arg) {
    int ret;
    ret = init_listen_recv();
    if (ret != 0) {
        log_err("init lisern recv error");
    }

    post_recv(ctx.comp_channel);

    return NULL;
}