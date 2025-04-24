#include <string.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "chain_buffer.h"

struct buf_chain_s {
    struct buf_chain_s *next;   //指向下一个缓冲区链
    uint32_t buffer_len;   //表示当前缓冲区的长度
    uint32_t misalign;     //表示偏移量,可用的起始位置,也就是防止反复腾挪数据，从这个位置开始才是有效数据
    uint32_t off;          //表示有效数据的长度
    uint8_t *buffer;
};

struct buffer_s {      //管理缓冲区链的结构
    buf_chain_t *first;
    buf_chain_t *last;
    //这是一个指向 buf_chain_t 指针的指针。它可能用于记录最后一个包含有效数据的缓冲区的指针。在某些操作中，需要快速找到最后一个有数据的缓冲区，使用这个指针可以提高查找效率
    buf_chain_t **last_with_datap;
    //表示整个缓冲区链中所有有效数据的总长度
    uint32_t total_len;
    //用于分隔读取操作。在按特定分隔符读取数据时，记录上一次读取的位置，以便后续继续从该位置开始读取数据
    uint32_t last_read_pos; // for sep read
};

//该宏用于计算一个 buf_chain_s 结构体所代表的缓冲区中，剩余可用于存储数据的空间大小
#define CHAIN_SPACE_LEN(ch) ((ch)->buffer_len - ((ch)->misalign + (ch)->off))
//定义了缓冲区的最小大小为 1024 字节
#define MIN_BUFFER_SIZE 1024
//表示在扩展缓冲区时，最多可以复制的数据量为 4096 字节。这可以防止在扩展缓冲区时复制过多的数据，避免性能问题
#define MAX_TO_COPY_IN_EXPAND 4096
//定义了缓冲区链自动扩展的最大大小为 4096 字节。当缓冲区链需要扩展时，其大小不会超过这个值
#define BUFFER_CHAIN_MAX_AUTO_SIZE 4096
//表示在扩展缓冲区时，最多可以重新对齐的数据量为 2048 字节
#define MAX_TO_REALIGN_IN_EXPAND 2048
//定义了整个缓冲区链的最大大小为 16MB
#define BUFFER_CHAIN_MAX 16*1024*1024  // 16M
//该宏用于获取 buf_chain_t 结构体之后的额外数据
#define BUFFER_CHAIN_EXTRA(t, c) (t *)((buf_chain_t *)(c) + 1)
//定义了 buf_chain_t 结构体的大小
#define BUFFER_CHAIN_SIZE sizeof(buf_chain_t)

/*
参数：
buf：指向 buffer_t 类型缓冲区的指针。
返回值：缓冲区中数据的总长度。
*/
uint32_t buffer_len(buffer_t *buf) {
    // 返回缓冲区中数据的总长度
    return buf->total_len;
}

/*
功能：创建一个新的 buffer_t 类型的缓冲区。
参数：
sz：此参数在函数中未使用，可忽略。
返回值：
如果内存分配成功，返回指向新创建的 buffer_t 缓冲区的指针。
如果内存分配失败，返回 NULL。
*/
buffer_t *buffer_new(uint32_t sz) {
    // 忽略传入的 sz 参数
    (void)sz;
    // 为 buffer_t 结构体分配内存
    buffer_t * buf = (buffer_t *) malloc(sizeof(buffer_t));
    // 检查内存分配是否成功
    if (!buf) {
        return NULL;
    }
    // 将分配的内存初始化为 0
    memset(buf, 0, sizeof(*buf));
    // 将 last_with_datap 指向 first
    buf->last_with_datap = &buf->first;
    // 返回新创建的缓冲区指针
    return buf;
}

/*
功能：创建一个新的 buf_chain_t 类型的缓冲区链。
参数：
size：要分配的缓冲区大小。
返回值：
如果内存分配成功，返回指向新创建的 buf_chain_t 缓冲区链的指针。
如果内存分配失败或大小超过最大限制，返回 NULL。
*/
static buf_chain_t *buf_chain_new(uint32_t size) {
    buf_chain_t *chain;
    uint32_t to_alloc;
    // 检查要分配的大小是否超过最大限制
    if (size > BUFFER_CHAIN_MAX - BUFFER_CHAIN_SIZE)
        return (NULL);
    // 计算实际需要分配的大小，包含 buf_chain_t 结构体的大小
    size += BUFFER_CHAIN_SIZE;
    
    // 如果要分配的大小小于最大大小的一半
    if (size < BUFFER_CHAIN_MAX / 2) {
        // 初始分配大小为最小缓冲区大小
        to_alloc = MIN_BUFFER_SIZE;
        // 不断将分配大小左移，直到大于等于要分配的大小
        while (to_alloc < size) {
            to_alloc <<= 1;
        }
    } else {
        // 否则，分配大小为要分配的大小
        to_alloc = size;
    }
    // 分配内存
    if ((chain = malloc(to_alloc)) == NULL)
        return (NULL);
    // 将分配的内存初始化为 0
    memset(chain, 0, BUFFER_CHAIN_SIZE);
    // 设置缓冲区的长度
    chain->buffer_len = to_alloc - BUFFER_CHAIN_SIZE;
    // 设置缓冲区数据的起始地址
    chain->buffer = BUFFER_CHAIN_EXTRA(uint8_t, chain);
    // 返回新创建的缓冲区链指针
    return (chain);
}

/*
功能：释放整个 buf_chain_t 类型的缓冲区链。
参数：
chain：指向要释放的缓冲区链的第一个节点的指针。
返回值：无。
*/
static void buf_chain_free_all(buf_chain_t *chain) {
    buf_chain_t *next;
    // 遍历缓冲区链
    for (; chain; chain = next) {
        // 保存下一个缓冲区链的指针
        next = chain->next;
        // 释放当前缓冲区链的内存
        free(chain);
    }
}

/*
功能：释放 buffer_t 类型缓冲区中的所有缓冲区链。
参数：
buf：指向要释放的 buffer_t 缓冲区的指针。
返回值：无。
*/
void buffer_free(buffer_t *buf) {
    // 释放缓冲区中的所有缓冲区链
    buf_chain_free_all(buf->first);
}

/*
功能：释放 buffer_t 缓冲区中所有空的缓冲区链。
参数：
buf：指向 buffer_t 缓冲区的指针。
返回值：最后一个有数据的缓冲区链指针的地址。
*/
static buf_chain_t **free_empty_chains(buffer_t *buf) {
    // 获取最后一个有数据的缓冲区链指针的地址
    buf_chain_t **ch = buf->last_with_datap;
    // 遍历缓冲区链，直到找到一个空的缓冲区链
    while ((*ch) && (*ch)->off != 0)
        ch = &(*ch)->next;
    // 如果找到空的缓冲区链
    if (*ch) {
        // 释放从该空缓冲区链开始的所有缓冲区链
        buf_chain_free_all(*ch);
        // 将该指针置为 NULL
        *ch = NULL;
    }
    // 返回最后一个有数据的缓冲区链指针的地址
    return ch;
}

/*
功能：将一个新的 buf_chain_t 缓冲区链插入到 buffer_t 缓冲区中。
参数：
buf：指向 buffer_t 缓冲区的指针。
chain：指向要插入的 buf_chain_t 缓冲区链的指针。
返回值：无。
*/
static void buf_chain_insert(buffer_t *buf, buf_chain_t *chain) {
    // 如果最后一个有数据的缓冲区链为空
    if (*buf->last_with_datap == NULL) {
        // 将第一个和最后一个缓冲区链都设置为新的缓冲区链
        buf->first = buf->last = chain;
    } else {
        buf_chain_t **chp;
        // 释放所有空的缓冲区链
        chp = free_empty_chains(buf);
        // 将新的缓冲区链插入到最后一个有数据的缓冲区链之后
        *chp = chain;
        // 如果新的缓冲区链有数据
        if (chain->off)
            // 更新最后一个有数据的缓冲区链指针的地址
            buf->last_with_datap = chp;
        // 更新最后一个缓冲区链为新的缓冲区链
        buf->last = chain;
    }
    // 更新缓冲区中数据的总长度
    buf->total_len += chain->off;
}

/*
功能：创建一个新的 buf_chain_t 缓冲区链，并将其插入到 buffer_t 缓冲区中。
参数：
buf：指向 buffer_t 缓冲区的指针。
datlen：要分配的缓冲区大小。
返回值：
如果创建和插入成功，返回指向新插入的 buf_chain_t 缓冲区链的指针。
如果创建失败，返回 NULL。
*/
static inline buf_chain_t *buf_chain_insert_new(buffer_t *buf, uint32_t datlen) {
    buf_chain_t *chain;
    // 创建一个新的缓冲区链
    if ((chain = buf_chain_new(datlen)) == NULL)
        return NULL;
    // 将新的缓冲区链插入到缓冲区中
    buf_chain_insert(buf, chain);
    // 返回新插入的缓冲区链指针
    return chain;
}

/*
功能：判断一个 buf_chain_t 缓冲区链是否需要重新对齐。
参数：
chain：指向 buf_chain_t 缓冲区链的指针。
datlen：要添加的数据长度。
返回值：
如果满足重新对齐的条件，返回非零值。
否则，返回 0。
*/
static int
buf_chain_should_realign(buf_chain_t *chain, uint32_t datlen)
{
    // 判断是否满足重新对齐的条件
    return chain->buffer_len - chain->off >= datlen &&
        (chain->off < chain->buffer_len / 2) &&
        (chain->off <= MAX_TO_REALIGN_IN_EXPAND);
}

/*
功能：对 buf_chain_t 缓冲区链进行重新对齐，将数据移动到缓冲区的起始位置。
参数：
chain：指向 buf_chain_t 缓冲区链的指针。
返回值：无。
*/
static void
buf_chain_align(buf_chain_t *chain) {
    // 将数据移动到缓冲区的起始位置
    memmove(chain->buffer, chain->buffer + chain->misalign, chain->off);
    // 将偏移量设置为 0
    chain->misalign = 0;
}

/*
功能：向 buffer_t 缓冲区中添加数据。
参数：
buf：指向 buffer_t 缓冲区的指针。
data_in：指向要添加的数据的指针。
datlen：要添加的数据长度。
返回值：
如果添加成功，返回 0。
如果添加失败，返回 -1。
*/
int buffer_add(buffer_t *buf, const void *data_in, uint32_t datlen) {
    buf_chain_t *chain, *tmp;
    const uint8_t *data = data_in;
    uint32_t remain, to_alloc;
    int result = -1;
    // 检查要添加的数据长度是否超过最大限制
    if (datlen > BUFFER_CHAIN_MAX - buf->total_len) {
        goto done;
    }

    // 获取最后一个有数据的缓冲区链
    if (*buf->last_with_datap == NULL) {
        chain = buf->last;
    } else {
        chain = *buf->last_with_datap;
    }

    // 如果没有可用的缓冲区链
    if (chain == NULL) {
        // 创建一个新的缓冲区链并插入到缓冲区中
        chain = buf_chain_insert_new(buf, datlen);
        if (!chain)
            goto done;
    }

    // 计算当前缓冲区链剩余的可用空间
    remain = chain->buffer_len - chain->misalign - chain->off;
    // 如果剩余空间足够容纳要添加的数据
    if (remain >= datlen) {
        // 将数据复制到当前缓冲区链中
        memcpy(chain->buffer + chain->misalign + chain->off, data, datlen);
        // 更新缓冲区链的偏移量
        chain->off += datlen;
        // 更新缓冲区中数据的总长度
        buf->total_len += datlen;
        // buf->n_add_for_cb += datlen;
        goto out;
    } else if (buf_chain_should_realign(chain, datlen)) {
        // 如果需要重新对齐缓冲区链
        buf_chain_align(chain);

        // 将数据复制到重新对齐后的缓冲区链中
        memcpy(chain->buffer + chain->off, data, datlen);
        // 更新缓冲区链的偏移量
        chain->off += datlen;
        // 更新缓冲区中数据的总长度
        buf->total_len += datlen;
        // buf->n_add_for_cb += datlen;
        goto out;
    }
    // 计算要分配的新缓冲区大小
    to_alloc = chain->buffer_len;
    if (to_alloc <= BUFFER_CHAIN_MAX_AUTO_SIZE/2)
        to_alloc <<= 1;
    if (datlen > to_alloc)
        to_alloc = datlen;
    // 创建一个新的缓冲区链
    tmp = buf_chain_new(to_alloc);
    if (tmp == NULL)
        goto done;
    // 如果当前缓冲区链还有剩余空间
    if (remain) {
        // 将数据复制到当前缓冲区链的剩余空间中
        memcpy(chain->buffer + chain->misalign + chain->off, data, remain);
        // 更新缓冲区链的偏移量
        chain->off += remain;
        // 更新缓冲区中数据的总长度
        buf->total_len += remain;
        // buf->n_add_for_cb += remain;
    }

    // 移动数据指针
    data += remain;
    // 减少要添加的数据长度
    datlen -= remain;

    // 将剩余的数据复制到新的缓冲区链中
    memcpy(tmp->buffer, data, datlen);
    // 设置新缓冲区链的偏移量
    tmp->off = datlen;
    // 将新的缓冲区链插入到缓冲区中
    buf_chain_insert(buf, tmp);
    // buf->n_add_for_cb += datlen;
out:
    result = 0;
done:
    return result;
}

/*
功能：从链式缓冲区中复制数据到外部缓冲区。
参数：
buf：指向链式缓冲区管理结构体 buffer_t 的指针。
data_out：指向外部接收数据的缓冲区。
datlen：请求复制的数据长度。
返回值：实际复制的字节数。
*/
static uint32_t buf_copyout(buffer_t *buf, void *data_out, uint32_t datlen) {
    buf_chain_t *chain;          // 当前缓冲区链节点
    char *data = data_out;        // 输出数据指针（转换为char*方便操作）
    uint32_t nread;               // 实际读取的总字节数
    chain = buf->first;           // 从第一个缓冲区链开始
    if (datlen > buf->total_len)  // 若请求长度超过缓冲区总长度，取总长度
        datlen = buf->total_len;
    if (datlen == 0)              // 无数据可读，直接返回
        return 0;
    nread = datlen;               // 记录原始请求长度
    
    // 遍历缓冲区链，复制数据
    while (datlen && datlen >= chain->off) {  // 当还有数据需要读取且当前链数据足够
        uint32_t copylen = chain->off;        // 当前链可复制的字节数（off表示当前链中有效数据长度）
        // 复制数据：从当前链的缓冲区起始位置 + 偏移量（misalign）开始复制
        memcpy(data, 
               chain->buffer + chain->misalign,  // 源地址：当前链数据起始位置
               copylen);                        // 复制长度
        data += copylen;                        // 移动输出数据指针
        datlen -= copylen;                      // 减少剩余需要复制的长度
        chain = chain->next;                    // 移动到下一个缓冲区链
    }
    if (datlen) {                               // 若剩余数据未复制完（当前链数据不足）
        // 复制当前链中剩余的datlen字节（此时datlen < chain->off）
        memcpy(data, chain->buffer + chain->misalign, datlen);
    }
    return nread;                               // 返回实际读取的总字节数（可能等于或小于请求长度）
}

/*
功能：重置链式缓冲区管理结构体，清空所有状态。
参数：
dst：指向需要重置的 buffer_t 结构体。
返回值：无。
*/
static inline void ZERO_CHAIN(buffer_t *dst) {
    dst->first = NULL;                // 清空第一个缓冲区链指针
    dst->last = NULL;                 // 清空最后一个缓冲区链指针
    dst->last_with_datap = &(dst)->first;  // 重置最后一个有数据的链指针指向第一个位置
    dst->total_len = 0;               // 总数据长度清零
}

/*
功能：从链式缓冲区中释放（删除）指定长度的数据，释放相关内存。
参数：
buf：指向链式缓冲区管理结构体。
len：请求释放的数据长度。
返回值：实际释放的字节数。
*/
int buffer_drain(buffer_t *buf, uint32_t len) {
    buf_chain_t *chain, *next;        // 当前链和下一个链指针
    uint32_t remaining, old_len;      // 剩余需要释放的长度、原始总长度
    old_len = buf->total_len;         // 记录原始总数据长度
    if (old_len == 0)                 // 缓冲区为空，直接返回
        return 0;
    
    if (len >= old_len) {             // 若释放长度大于等于总长度，清空所有链
        len = old_len;                // 实际释放长度为总长度
        for (chain = buf->first; chain != NULL; chain = next) {  // 遍历所有链
            next = chain->next;       // 保存下一个链指针
            free(chain);              // 释放当前链内存
        }
        ZERO_CHAIN(buf);              // 重置缓冲区状态
    } else {                          // 释放部分数据（len < old_len）
        buf->total_len -= len;        // 更新总长度（减去释放的长度）
        remaining = len;              // 剩余需要释放的长度
        // 遍历链，直到剩余长度大于当前链的有效数据长度
        for (chain = buf->first; remaining >= chain->off; chain = next) {
            next = chain->next;       // 保存下一个链指针
            remaining -= chain->off;  // 减去当前链的有效数据长度
            // 更新最后一个有数据的链指针（若当前链是最后一个有数据的链）
            if (chain == *buf->last_with_datap) {
                buf->last_with_datap = &buf->first;  // 重置为第一个链（可能为空）
            }
            // 若当前链的下一个链是最后一个有数据的链指针指向的位置
            if (&chain->next == buf->last_with_datap) {
                buf->last_with_datap = &buf->first;  // 重置
            }
            free(chain);              // 释放当前链内存
        }
        // 处理剩余未释放完的链（当前链还有部分数据保留）
        buf->first = chain;           // 第一个链更新为剩余数据的起始链
        chain->misalign += remaining; // 调整当前链的偏移量（跳过已释放的数据）
        chain->off -= remaining;      // 当前链的有效数据长度减少
    }
    // buf->n_del_for_cb += len;  // （注释掉的回调计数，实际功能忽略）
    return len;                       // 返回实际释放的长度
}

/*
功能：从链式缓冲区中移除指定长度的数据，先复制数据再释放内存。
参数：
buf：指向链式缓冲区管理结构体。
data_out：接收移除数据的外部缓冲区。
datlen：请求移除的数据长度。
返回值：
正数：实际移除的字节数。
-1：操作失败（如释放内存失败）。
*/
int buffer_remove(buffer_t *buf, void *data_out, uint32_t datlen) {
    uint32_t n = buf_copyout(buf, data_out, datlen);  // 先复制数据到外部缓冲区
    if (n > 0) {                                      // 若复制成功
        if (buffer_drain(buf, n) < 0)                 // 释放已复制的数据
            n = -1;                                   // 释放失败，返回错误码
    }
    return (int)n;                                    // 返回实际移除的字节数（或-1表示失败）
}

/*
功能：在链式缓冲区的某个链节点中，从指定位置开始检查是否存在目标分隔符（支持跨链匹配）。
参数：
chain：当前链节点。
from：当前链中开始检查的位置偏移。
sep：目标分隔符。
seplen：分隔符长度。
返回值：
true：找到分隔符。
false：未找到。
*/
static bool check_sep(buf_chain_t * chain, int from, const char *sep, int seplen) {
    for (;;) {                                      // 无限循环，直到找到或遍历完
        int sz = chain->off - from;                // 当前链中从from位置开始的剩余数据长度
        if (sz >= seplen) {                        // 当前链剩余数据足够容纳分隔符
            // 比较当前链中from位置开始的seplen字节是否等于分隔符
            return memcmp(chain->buffer + chain->misalign + from, sep, seplen) == 0;
        }
        if (sz > 0) {                               // 当前链有部分数据可比较
            // 比较当前链中from位置开始的sz字节，若不匹配分隔符前缀，返回false
            if (memcmp(chain->buffer + chain->misalign + from, sep, sz)) {
                return false;
            }
        }
        // 移动到下一个链，更新分隔符指针和剩余长度
        chain = chain->next;                        // 下一个链
        sep += sz;                                  // 分隔符指针后移已匹配的sz字节
        seplen -= sz;                               // 剩余需要匹配的分隔符长度
        from = 0;                                   // 下一个链从0位置开始匹配
    }
}

/*
功能：在链式缓冲区中搜索指定分隔符，支持跨链节点匹配。
参数：
buf：指向链式缓冲区管理结构体。
sep：目标分隔符字符串。
seplen：分隔符长度。
返回值：
正数：分隔符结束位置（相对于缓冲区起始）。
0：未找到。
*/
int buffer_search(buffer_t *buf, const char* sep, const int seplen) {
    buf_chain_t *chain;              // 当前链节点
    int i;                           // 搜索位置索引
    chain = buf->first;              // 从第一个链开始
    if (chain == NULL)               // 缓冲区为空，返回0
        return 0;
    int bytes = chain->off;          // 当前链的有效数据长度
    // 跳过已搜索过的位置（从last_read_pos开始）
    while (bytes <= buf->last_read_pos) {
        chain = chain->next;         // 移动到下一个链
        if (chain == NULL)           // 到达链尾，未找到
            return 0;
        bytes += chain->off;         // 累加后续链的有效数据长度
    }
    bytes -= buf->last_read_pos;     // 计算当前链中实际需要搜索的起始位置偏移
    int from = chain->off - bytes;   // 当前链中搜索的起始位置（相对于链内偏移）
    // 遍历缓冲区总长度，查找分隔符
    for (i = buf->last_read_pos; i <= buf->total_len - seplen; i++) {
        if (check_sep(chain, from, sep, seplen)) {  // 检查是否存在分隔符
            buf->last_read_pos = 0;                 // 重置上次读取位置
            return i + seplen;                      // 返回分隔符结束位置
        }
        ++from;                                    // 当前链内位置后移
        --bytes;                                    // 剩余需要搜索的字节数减少
        if (bytes == 0) {                           // 当前链搜索完毕
            chain = chain->next;                    // 移动到下一个链
            from = 0;                               // 下一个链从0位置开始
            if (chain == NULL)                       // 链尾，退出循环
                break;
            bytes = chain->off;                      // 重置为下一个链的有效数据长度
        }
    }
    buf->last_read_pos = i;                          // 记录最后搜索位置
    return 0;                                       // 未找到分隔符
}

/*
功能：返回链式缓冲区中可写入的最大连续空间的起始地址，可能通过重组缓冲区实现连续空间。
参数：
p：指向链式缓冲区管理结构体。
返回值：
指向可写入的连续缓冲区的起始地址。
NULL：内存分配失败。
*/
uint8_t * buffer_write_atmost(buffer_t *p) {
    buf_chain_t *chain, *next, *tmp, *last_with_data;  // 链节点指针
    uint8_t *buffer;                                   // 返回的可写入缓冲区指针
    uint32_t remaining;                                // 剩余需要重组的数据长度
    int removed_last_with_data = 0;                    // 标记是否移除了最后一个有数据的链
    int removed_last_with_datap = 0;                   // 标记是否移除了最后一个有数据的链指针

    chain = p->first;                                  // 从第一个链开始
    uint32_t size = p->total_len;                      // 缓冲区总数据长度

    if (chain->off >= size) {                          // 若第一个链的有效数据等于总长度（单链且连续）
        return chain->buffer + chain->misalign;         // 直接返回当前链的可写入位置
    }

    remaining = size - chain->off;                     // 计算第一个链之后的剩余数据长度
    // 找到第一个链之后，数据长度足够容纳剩余数据的链
    for (tmp = chain->next; tmp; tmp = tmp->next) {
        if (tmp->off >= (size_t)remaining)
            break;
        remaining -= tmp->off;
    }

    // 情况一：第一个链有足够空间容纳所有数据（重组后无需新链）
    if (chain->buffer_len - chain->misalign >= (size_t)size) {
        size_t old_off = chain->off;                   // 记录当前链原有有效数据长度
        buffer = chain->buffer + chain->misalign + chain->off;  // 可写入位置
        tmp = chain;                                    // 临时保存当前链
        tmp->off = size;                                // 更新当前链的有效数据长度（扩展后）
        size -= old_off;                                // 剩余需要移动的数据长度（实际为0，因为空间足够）
        chain = chain->next;                            // 移动到下一个链（可能为空）
    } else {                                            // 情况二：需要创建新链来容纳数据
        if ((tmp = buf_chain_new(size)) == NULL) {      // 创建新链
            return NULL;                                // 内存分配失败
        }
        buffer = tmp->buffer;                           // 新链的缓冲区起始位置
        tmp->off = size;                                // 新链的有效数据长度为总长度
        p->first = tmp;                                 // 新链作为第一个链
    }

    last_with_data = *p->last_with_datap;               // 记录最后一个有数据的链

    // 遍历旧链，将数据复制到新链或重组后的链中
    for (; chain != NULL && (size_t)size >= chain->off; chain = next) {
        next = chain->next;                             // 保存下一个链指针
        if (chain->buffer) {                            // 若当前链有数据缓冲区
            // 复制当前链的数据到目标缓冲区（新链或重组后的链）
            memcpy(buffer, chain->buffer + chain->misalign, chain->off);
            size -= chain->off;                         // 减少剩余需要复制的长度
            buffer += chain->off;                       // 移动目标缓冲区指针
        }
        // 标记是否移除了最后一个有数据的链
        if (chain == last_with_data)
            removed_last_with_data = 1;
        // 标记是否移除了最后一个有数据的链指针指向的位置
        if (&chain->next == p->last_with_datap)
            removed_last_with_datap = 1;
        free(chain);                                    // 释放旧链内存
    }

    if (chain != NULL) {                                // 若还有未处理的链（剩余部分数据）
        // 复制剩余数据到当前链（调整偏移量，不释放链）
        memcpy(buffer, chain->buffer + chain->misalign, size);
        chain->misalign += size;                        // 调整当前链的偏移量（跳过已复制的数据）
        chain->off -= size;                             // 当前链的有效数据长度减少
    } else {                                            // 所有旧链处理完毕，新链为最后一个链
        p->last = tmp;                                  // 更新最后一个链为新链
    }

    tmp->next = chain;                                  // 新链连接剩余未处理的链

    // 更新最后一个有数据的链指针
    if (removed_last_with_data) {
        p->last_with_datap = &p->first;                 // 重置为第一个链
    } else if (removed_last_with_datap) {
        // 根据后续链是否有数据，调整最后一个有数据的链指针
        if (p->first->next && p->first->next->off)
            p->last_with_datap = &p->first->next;
        else
            p->last_with_datap = &p->first;
    }
    return tmp->buffer + tmp->misalign;                // 返回新链的可写入起始位置
}