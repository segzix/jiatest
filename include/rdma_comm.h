#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H
#include "msg_queue.h"
#include <infiniband/verbs.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <stdbool.h>

#define Maxhosts 2
#define BatchingSize 8
#define QueueSize 32
#define BatchingNum (QueueSize / BatchingSize)

typedef struct rdma_connect {
    bool connected;
    struct rdma_cm_id id;
    struct ibv_mr **in_mr;
    msg_queue_t *inqueue;
} rdma_connect_t;

typedef struct jia_context {
    struct ibv_context *context;
    struct ibv_pd *pd;                      // common pd
    struct ibv_comp_channel *comp_channel;  // common io completion channel

    // port related
    int ib_port;                   // ib port number
    struct ibv_port_attr portinfo; // port information

    // info data
    int send_flags;   // describe send_wr's attributes {IBV_SEND_FENCE,
                      // IBV_SEND_SIGNALED, IBV_SEND_SOLICITED, IBV_SEND_INLINE,
                      // IBV_SEND_IP_CSUM}
    int batching_num; // post recv wr doorbell batching num

    // rdma connect
    msg_queue_t *outqueue;
    struct ibv_mr *out_mr[QueueSize];
    rdma_connect_t connect_array[Maxhosts];

    // connection parameters
    int tcp_port;              // tcp port number
    pthread_t server_thread;   // server thread for connection from client on other hosts
    pthread_t *client_threads; // client threads used to connect other hosts
} jia_context_t;

extern jia_context_t ctx;

/* function declaration */

/**
 * @brief server_thread -- server thread used to handle connection (rdmacm)
 */
void *server_thread(void *arg);

/**
 * @brief client_thread -- client thread used to connect to server (rdmacm)
 */
void *client_thread(void *arg);

/**
 * @brief rdma_listen -- rdma listen thread (post recv wr by doorbell batching)
 */
void *rdma_listen_thread(void *arg);

/**
 * @brief rdma_client -- rdma client thread (post send wr)
 */
void *rdma_client_thread(void *arg);

/**
 * @brief rdma_server -- rdma server thread (handle msg)
 */
void *rdma_server_thread(void *arg);

/**
 * @brief init_rdma_context -- init rdma context
 * @param context -- rdma context
 * @param batching_num -- post recv wr doorbell batching num
 */
void init_rdma_context(struct jia_context *context, int batching_num);

/**
 * @brief init_rdma_resource -- init rdma communication resources
 *
 * @param context
 */
void init_rdma_resource(struct jia_context *context);

/**
 * @brief free_rdma_resources -- free rdma related resources
 * @param ctx -- jiajia rdma context
 */
void free_rdma_resources(struct jia_context *ctx);
#endif