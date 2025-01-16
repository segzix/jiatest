#include "msg_queue.h"
#include "rdma_comm.h"
#include "tools.h"
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

extern int snd_seq[Maxhosts];

// these need to reconfiguration on another host
#ifdef MASTER
    const char *server_ip = "192.168.103.1";
    const char *client_ip = "192.168.103.2";
    int total_hosts = 2;
    int jia_pid = 0;
    int to_pid = 1;
#else
    const char *server_ip = "192.168.103.2";
    const char *client_ip = "192.168.103.1";
    int total_hosts = 2;
    int jia_pid = 1;
    int to_pid = 0;
#endif

FILE *logfile;

void sync_connection(int total_hosts, int jia_pid);
void generate_random_string(char *dest, size_t length);
int move_msg_to_outqueue(jia_msg_t *msg, msg_queue_t *outqueue);

int main()
{
    int batching_num = 8;
    if(open_logfile("jiajia.log")) {
        log_err("Unable to open jiajia.log");
        exit(-1);
    }
    setbuf(logfile, NULL);

    init_rdma_context(&ctx, batching_num);

    sync_connection(total_hosts, jia_pid);

    init_rdma_resource(&ctx);

    pthread_create(&rdma_listen_tid, NULL, rdma_listen_thread, NULL);
    pthread_create(&rdma_client_tid, NULL, rdma_client_thread, NULL);
    pthread_create(&rdma_server_tid, NULL, rdma_server_thread, NULL);


    jia_msg_t msg;
    while(1) {
        msg.op = WAIT;
        msg.frompid = jia_pid;
        msg.topid = to_pid;
        msg.temp = -1;
        msg.seqno = snd_seq[msg.topid];
        msg.index = 0;
        msg.scope = 0;
        msg.size = 16;
        generate_random_string((char *)msg.data, SIZE);
        
        move_msg_to_outqueue(&msg, &outqueue);
        // sleep(2);
    }
}

void sync_connection(int total_hosts, int jia_pid) {
    // 创建服务器线程（如果需要）
    if (jia_pid != 0) {
        pthread_create(&ctx.server_thread, NULL, server_thread, &ctx);
    }

    // 创建客户端线程（如果需要）
    int num_clients = total_hosts - jia_pid - 1;
    if (num_clients > 0) {
        ctx.client_threads = malloc(num_clients * sizeof(pthread_t));
        for (int i = 0; i < num_clients; i++) {
            int *target_host = (int *)malloc(sizeof(int));
            *target_host = jia_pid + i + 1;
            pthread_create(&ctx.client_threads[i], NULL, client_thread, target_host);
        }
    }

    // 等待服务器线程
    if (jia_pid != 0) {
        pthread_join(ctx.server_thread, NULL);
    }

    // 等待所有客户端线程
    if (num_clients > 0) {
        for (int i = 0; i < num_clients; i++) {
            pthread_join(ctx.client_threads[i], NULL);
        }
    }
}

void generate_random_string(char *dest, size_t length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"; // 字符集
    size_t charset_size = sizeof(charset) - 1; // 不包括末尾的 '\0'

    // 获取时间和进程 ID 作为种子
    srand((unsigned int)(time(NULL) ^ getpid()));

    // 生成随机字符串并存储到 dest 中
    for (size_t i = 0; i < length - 1; ++i) { // 留出最后一个字符的位置给 '\0'
        dest[i] = charset[rand() % charset_size];
    }

    dest[length - 1] = '\0'; // 添加字符串结束符
    log_out(3, "generate string: %s", dest);
}

int move_msg_to_outqueue(jia_msg_t *msg, msg_queue_t *outqueue) {
    int ret = enqueue(outqueue, msg);
    if (ret == -1) {
        perror("enqueue");
        return ret;
    }
    return 0;
}