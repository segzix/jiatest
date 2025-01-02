#include "rdma_comm.h"
#include "tools.h"
#include <arpa/inet.h>
#include <endian.h>
#include <infiniband/verbs.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


#define Msgsize 200 // temp value, used inlined message size in rdma

extern int jia_pid;
extern int ack_send, ack_recv;
extern const char *server_ip;
extern const char *client_ip;
int batching_num = 8;
long start_port = 22222;

jia_context_t ctx;
jia_dest_t dest_info[Maxhosts];
int arg[Maxhosts];

void *tcp_server_thread(void *arg) {
    /* step 1: create server tcp socket fd (setting reuse) */
    int server_id = *(int *)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_err("Failed to create server socket");
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                   sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR/SO_REUSEPORT failed");
        exit(EXIT_FAILURE);
    }

    /* step 2: bind fd and listen to addr::port (ANY_ADDR) */
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr =
        inet_addr((char *)server_ip);
    server_addr.sin_port = htons(ctx.tcp_port + server_id);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 1) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    log_out(3, "Server listening on <%s, %d>...\n",
            server_ip,
            ctx.tcp_port + server_id);

    /* step 3: accept ANY_ADDR to set new fd */
    struct sockaddr_in client_addr = {0};
    socklen_t addrlen = sizeof(client_addr);
    int new_socket =
        accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
    if (new_socket < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }
    log_out(3, "Server: connection established with client.\n");

    /* step 4: test connection */
    bool flag = true;
    while (1) {
        ssize_t bytes_read =
            recv(new_socket, &dest_info[server_id], sizeof(jia_dest_t), 0);
        if (bytes_read == sizeof(jia_dest_t)) {
            send(new_socket, &flag, sizeof(bool), 0);
            break;
        } else {
            flag = false;
            send(new_socket, &flag, sizeof(bool), 0);
        }
    }
    log_out(3, "[%d]\t0x%04x\t0x%06x\t0x%06x\t%016llx:%016llx\n", server_id,
            dest_info[server_id].lid, dest_info[server_id].qpn,
            dest_info[server_id].psn,
            dest_info[server_id].gid.global.subnet_prefix,
            dest_info[server_id].gid.global.interface_id);

    /* step 5: close fd */
    close(new_socket);
    close(server_fd);
    return NULL;
}

void *tcp_client_thread(void *arg) {
    /* step 1: create client tcp socket fd (setting timeout) */
    int client_id = *(int *)arg;
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        log_err("Failed to create client socket");
    }
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout,
                   sizeof(timeout)) < 0) {
        perror("setsockopt failed");
        close(client_fd);
    }

    /* step 2: connect server_addr */
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(client_ip);
    server_addr.sin_port = htons(ctx.tcp_port + jia_pid);
    while (connect(client_fd, (struct sockaddr *)&server_addr,
                   sizeof(server_addr)) < 0) {
        if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
            log_err("Connection timed out\n");
        }
    }

    /* step 3: test connection */
    bool flag = false;
    while (1) {
        send(client_fd, &dest_info[jia_pid], sizeof(jia_dest_t),
             0);
        if (recv(client_fd, &flag, sizeof(bool), 0) == 1 && flag) {
            log_out(3, "server returned %s\n", flag == true ? "true" : "false");
            break;
        }
    }

    /* step 4: close fd */
    close(client_fd);
    return NULL;
}

void exchange_dest_info() {
    /* step 1: init thread args */
    for (int i = 0; i < Maxhosts; i++) {
        arg[i] = i;
    }

    /* step 2: create server and client threads */
    for (int i = 0; i < Maxhosts; i++) {
        if (i == jia_pid) {
            continue;
        }
        pthread_create(&ctx.server_tid[i], NULL, tcp_server_thread, &arg[i]);
    }
    for (int i = 0; i < Maxhosts; i++) {
        if (i == jia_pid) {
            continue;
        }
        pthread_create(&ctx.client_tid[i], NULL, tcp_client_thread, &arg[i]);
    }

    /* step 3: wait server and client threads */
    for (int i = 0; i < Maxhosts; i++) {
        if (i == jia_pid) {
            continue;
        }
        pthread_join(ctx.server_tid[i], NULL);
        pthread_join(ctx.client_tid[i], NULL);
    }

    printf("jia_pid\tlid\tqpn\tpsn\tgid.subnet_prefix:gid.interface_id\n");
    for (int i = 0; i < Maxhosts; i++) {
        printf("[%d]\t0x%04x\t0x%06x\t0x%06x\t%016llx:%016llx\n", i,
               dest_info[i].lid, dest_info[i].qpn, dest_info[i].psn,
               dest_info[i].gid.global.subnet_prefix,
               dest_info[i].gid.global.interface_id);
    }
}

void init_comm_qp_context(struct jia_context *ctx) {
    /* step 1: create completion channel */
    ctx->send_channel = ibv_create_comp_channel(ctx->context);
    ctx->recv_channel = ibv_create_comp_channel(ctx->context);

    /* step 2: register memory region */
    ctx->send_mr = (struct ibv_mr **)malloc(sizeof(struct ibv_mr *) *
                                            SIZE);
    ctx->recv_mr = (struct ibv_mr **)malloc(sizeof(struct ibv_mr *) *
                                            SIZE);
    for (int i = 0; i < SIZE; i++) {
        ctx->send_mr[i] = ibv_reg_mr(ctx->pd, &ctx->outqueue->queue[i].msg,
                                     sizeof(jia_msg_t), IBV_ACCESS_LOCAL_WRITE);
        ctx->recv_mr[i] = ibv_reg_mr(ctx->pd, &ctx->inqueue->queue[i].msg,
                                     sizeof(jia_msg_t), IBV_ACCESS_LOCAL_WRITE);
    }

    /* step 3: create completion queue */
    ctx->send_cq = ibv_create_cq(ctx->context, SIZE,
                                 NULL, ctx->send_channel, 0);
    ctx->recv_cq = ibv_create_cq(ctx->context, SIZE,
                                 NULL, ctx->recv_channel, 0);

    /* step 4: create QP && change QP's state to RTS */
    {
        /* step 4.1: create qp with giving type and cap */
        struct ibv_qp_attr attr;
        struct ibv_qp_init_attr init_attr = {
            .qp_context = ctx->context,
            .send_cq = ctx->send_cq,
            .recv_cq = ctx->recv_cq,
            .srq = NULL,
            .cap =
                {
                      .max_send_wr = SIZE,
                      .max_recv_wr = SIZE,
                      .max_send_sge = 1,
                      .max_recv_sge = 1,
                      .max_inline_data = 60,
                      },
            .qp_type = IBV_QPT_UD,
            .sq_sig_all = 1
        };
        ctx->qp = ibv_create_qp(ctx->pd, &init_attr);

        /* step 4.2: query qp to get queue's info */
        ibv_query_qp(ctx->qp, &attr, IBV_QP_CAP, &init_attr);
        if (init_attr.cap.max_inline_data >= Msgsize) {
            // use inline data for little messages
            ctx->send_flags |= IBV_SEND_INLINE;
        }

        /* step 4.3: change state from IBV_QPS_INIT to IBV_QPS_RTS */
        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num = ctx->ib_port;
        attr.qkey = 0x11111111;
        if (ibv_modify_qp(ctx->qp, &attr,
                          IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                              IBV_QP_QKEY)) {
            log_err("Failed to modify QP to INIT");
        }

        attr.qp_state = IBV_QPS_RTR;
        if (ibv_modify_qp(ctx->qp, &attr, IBV_QP_STATE)) {
            log_err("Failed to modify QP to RTR");
        }

        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = 0;
        if (ibv_modify_qp(ctx->qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
            log_err("Failed to modify QP to RTS");
        }
    }

    /* step 5: update localhost's gid and other information in dest_info*/
    ibv_query_port(ctx->context, ctx->ib_port, &ctx->portinfo);
    dest_info[jia_pid].lid = ctx->portinfo.lid;
    dest_info[jia_pid].qpn = ctx->qp->qp_num;
    dest_info[jia_pid].psn = 0;

    if (ibv_query_gid(ctx->context, ctx->ib_port, 0,
                      &dest_info[jia_pid].gid)) {
        log_err("Failed to query gid");
        memset(&dest_info[jia_pid].gid, 0,
               sizeof(dest_info[jia_pid].gid));
    }
    dest_info[jia_pid].gid.global.subnet_prefix =
        be64toh(dest_info[jia_pid].gid.global.subnet_prefix);
    dest_info[jia_pid].gid.global.interface_id =
        be64toh(dest_info[jia_pid].gid.global.interface_id);

    log_info(3,
             "Local address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID "
             "%016llx:%016llx",
             dest_info[jia_pid].lid,
             dest_info[jia_pid].qpn,
             dest_info[jia_pid].psn,
             dest_info[jia_pid].gid.global.subnet_prefix,
             dest_info[jia_pid].gid.global.interface_id);

    // create ah
    ctx->ah = (struct ibv_ah **)malloc(sizeof(struct ibv_ah *) *
                                       Maxhosts);
    for (int i = 0; i < Maxhosts; i++) {
        if (i == jia_pid) {
            continue;
        }
        struct ibv_ah_attr attr = {.is_global = 0,
                                   .dlid = dest_info[i].lid,
                                   .sl = 0,
                                   .src_path_bits = 0,
                                   .port_num = ctx->ib_port};

        if (dest_info[i].gid.global.interface_id) {
            attr.is_global = 1;
            attr.grh.hop_limit = 1;
            attr.grh.dgid = dest_info[i].gid;
            attr.grh.sgid_index = 0;
        }

        ctx->ah[i] = ibv_create_ah(ctx->pd, &attr);
    }
}

void init_ack_qp_context(struct jia_context *ctx) {
    /* step 1: create completion channel */
    ctx->ack_send_channel = ibv_create_comp_channel(ctx->context);
    ctx->ack_recv_channel = ibv_create_comp_channel(ctx->context);

    /* step 2: register memory region */
    ctx->ack_send_mr =
        ibv_reg_mr(ctx->pd, &ack_send, sizeof(int), IBV_ACCESS_LOCAL_WRITE);
    ctx->ack_recv_mr =
        ibv_reg_mr(ctx->pd, &ack_recv, sizeof(int), IBV_ACCESS_LOCAL_WRITE);

    /* step 3: create completion queue */
    ctx->ack_send_cq =
        ibv_create_cq(ctx->context, 1, NULL, ctx->ack_send_channel, 0);
    ctx->ack_recv_cq =
        ibv_create_cq(ctx->context, 1, NULL, ctx->ack_recv_channel, 0);

    /* step 4: create QP && change QP's state to RTS */
    {
        /* step 4.1: create qp with giving type and cap */
        struct ibv_qp_attr attr;
        struct ibv_qp_init_attr init_attr = {
            .qp_context = ctx->context,
            .send_cq = ctx->ack_send_cq,
            .recv_cq = ctx->ack_recv_cq,
            .srq = NULL,
            .cap =
                {
                      .max_send_wr = 1,
                      .max_recv_wr = 1,
                      .max_send_sge = 1,
                      .max_recv_sge = 1,
                      .max_inline_data = 0,
                      },
            .qp_type = IBV_QPT_UD,
            .sq_sig_all = 1
        };
        ctx->ack_qp = ibv_create_qp(ctx->pd, &init_attr);

        // TODO: should choose immediate state?

        /* step 4.2: change state from IBV_QPS_INIT to IBV_QPS_RTS */
        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num = ctx->ib_port;
        attr.qkey = 0x11111111;
        if (ibv_modify_qp(ctx->ack_qp, &attr,
                          IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                              IBV_QP_QKEY)) {
            log_err("Failed to modify QP to INIT");
        }

        attr.qp_state = IBV_QPS_RTR;
        if (ibv_modify_qp(ctx->ack_qp, &attr, IBV_QP_STATE)) {
            log_err("Failed to modify QP to RTR");
        }

        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = 0;
        if (ibv_modify_qp(ctx->ack_qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
            log_err("Failed to modify QP to RTS");
        }
    }

    /* step 5: get ack_qpn */
    dest_info[jia_pid].ack_qpn = ctx->ack_qp->qp_num;
}

int init_rdma_context(struct jia_context *ctx, int batching_num) {
    struct ibv_device **dev_list;
    struct ibv_device *dev;

    /* step 1: get device list && get device context */
    dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
        log_info(3, "No RDMA device available");
    }
    dev = dev_list[0]; // use the first device
    ctx->context = ibv_open_device(dev);
    if (!dev) {
        log_info(3, "Open first device failed");
    }

    /* step 2: init ctx common parameters */
    ctx->pd = ibv_alloc_pd(ctx->context);
    ctx->outqueue = &outqueue;
    ctx->inqueue = &inqueue;
    ctx->ib_port = 2; // this can be set from a config file
    ctx->batching_num = batching_num;
    ctx->tcp_port = start_port;

    /* step 3: init communication QP context */
    init_comm_qp_context(ctx);

    /* step 4: init ACK QP context */
    init_ack_qp_context(ctx);

    /* step 5: exchange QP's info with other hosts */
    exchange_dest_info();

    return 0;
}

void free_rdma_resources(struct jia_context *ctx) {
    if (ibv_destroy_qp(ctx->qp)) {
        log_err("Couldn't destroy QP\n");
    }

    if (ibv_destroy_cq(ctx->send_cq)) {
        log_err("Couldn't destroy Send CQ\n");
    }

    if (ibv_destroy_cq(ctx->recv_cq)) {
        log_err("Couldn't destroy Recv CQ\n");
    }

    for (int i = 0; i < SIZE; i++) {
        if (ibv_dereg_mr(ctx->send_mr[i])) {
            log_err("Couldn't dereg send mr[%d]", i);
        }
        if (ibv_dereg_mr(ctx->recv_mr[i])) {
            log_err("Couldn't dereg recv mr[%d]", i);
        }
    }

    for (int i = 0; i < Maxhosts; i++) {
        if (ibv_destroy_ah(ctx->ah[i])) {
            log_err("Couldn't destroy ah[%d]", i);
        }
    }
    free(ctx->ah);

    if (ibv_dealloc_pd(ctx->pd)) {
        log_err("Couldn't deallocate PD");
    }

    if (ibv_destroy_comp_channel(ctx->send_channel)) {
        log_err("Couldn't destroy send completion channel");
    }

    if (ibv_destroy_comp_channel(ctx->recv_channel)) {
        log_err("Couldn't destroy recv completion channel");
    }

    if (ibv_close_device(ctx->context)) {
        log_err("Couldn't release context");
    }
}