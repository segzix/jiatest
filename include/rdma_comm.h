#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H
#include "msg_queue.h"
#include <infiniband/verbs.h>
#include <pthread.h>

#define Maxhosts 2

typedef struct jia_context {
	struct ibv_context	*context;
	struct ibv_pd		*pd;
	struct ibv_ah		**ah;

	struct ibv_comp_channel *send_channel;
    struct ibv_comp_channel *recv_channel;
	struct ibv_mr		**send_mr;
    struct ibv_mr       **recv_mr;
	struct ibv_cq		*send_cq;
    struct ibv_cq       *recv_cq;
	struct ibv_qp		*qp;

	struct ibv_comp_channel *ack_send_channel;
    struct ibv_comp_channel *ack_recv_channel;
	struct ibv_mr		*ack_send_mr;
    struct ibv_mr       *ack_recv_mr;
	struct ibv_cq		*ack_send_cq;
    struct ibv_cq       *ack_recv_cq;
	struct ibv_qp		*ack_qp;

	// port related
    int			 			ib_port;	// ib port number
	struct ibv_port_attr	portinfo;	// port information

	int			 send_flags;        // describe send_wr's attributes {IBV_SEND_FENCE, IBV_SEND_SIGNALED, IBV_SEND_SOLICITED, IBV_SEND_INLINE, IBV_SEND_IP_CSUM}
    int          batching_num;      // post recv wr doorbell batching num
	
    msg_queue_t *outqueue;
    msg_queue_t *inqueue;

    // connection parameters
    int          tcp_port;          	// tcp port number
    pthread_t    server_tid[Maxhosts];	// server thread id for eacho host
    pthread_t    client_tid[Maxhosts];	// client thread id for each host
} jia_context_t;

typedef struct jia_dest {
	int lid;
	int qpn;
	int psn;
	int ack_qpn;
	union ibv_gid gid;
} jia_dest_t;

extern jia_context_t ctx;
extern jia_dest_t dest_info[Maxhosts];	// store every destination host information

extern pthread_t rdma_client_tid;
extern pthread_t rdma_listen_tid;
extern pthread_t rdma_server_tid;

/* function declaration */

/**
 * @brief server_thread -- server thread used to handle connection (tcp)
 */
void *tcp_server_thread(void *arg);

/**
 * @brief client_thread -- client thread used to connect to server (tcp)
 */
void *tcp_client_thread(void *arg);


/**
 * @brief rdma_listen -- rdma listen thread (post recv wr by doorbell batching)
 */
void *rdma_listen(void *arg);

/**
 * @brief rdma_client -- rdma client thread (post send wr)
 */
void *rdma_client(void *arg);

/**
 * @brief rdma_server -- rdma server thread (handle msg)
 */
void *rdma_server(void *arg);


/**
 * @brief exchange_dest_info -- exchange dest info with other hosts
 */
void exchange_dest_info();

/**
 * @brief init_rdma_comm -- init rdma communication
 */
void init_rdma_comm();

/**
 * @brief init_rdma_context -- init rdma context
 * @param context -- rdma context
 * @param batching_num -- post recv wr doorbell batching num
 * @return 0 if success, -1 if failed
 */
int init_rdma_context(struct jia_context *context, int batching_num);


/**
 * @brief free_rdma_resources -- free rdma related resources
 * @param ctx -- jiajia rdma context
 */
void free_rdma_resources(struct jia_context *ctx);
#endif