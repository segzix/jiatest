# 设置编译器
CC = gcc

# 编译选项
CFLAGS = -Wall -g

LIBS = -libverbs -lpthread

# 头文件目录
INCLUDE_DIR = include

# 源文件目录
SRC_DIR = src
RDMA_SRC_DIR = rdma
MSG_SRC_DIR = msg

# 源文件
SRCS = $(SRC_DIR)/main.c \
       $(RDMA_SRC_DIR)/rdma_client.c \
       $(RDMA_SRC_DIR)/rdma_comm.c \
       $(RDMA_SRC_DIR)/rdma_listen.c \
       $(RDMA_SRC_DIR)/rdma_server.c \
       $(MSG_SRC_DIR)/msg_queue.c

# 目标文件
OBJS = $(SRCS:.c=.o)

# 可执行文件名
TARGET = test

# 依赖关系
DEPS = $(SRCS:.c=.d)

# 默认目标
all: $(TARGET)

# 生成可执行文件
$(TARGET): $(OBJS) 
	$(CC) $(OBJS) $(LIBS) -o $@

# 生成目标文件
%.o: %.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) $(LIBS) -c $< -o $@

# 生成依赖文件
%.d: %.c
	$(CC) -MM $(CFLAGS) -I$(INCLUDE_DIR) $< > $@

# 包含依赖文件
-include $(DEPS)

# 清理目标
clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

# 安装目标
install: $(TARGET)
	# 安装步骤，比如复制可执行文件到指定目录
	# cp $(TARGET) /usr/local/bin/

