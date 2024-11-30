#pragma once

#include<vector>
#include"buffer.h"

namespace safe_udp{
class SlidingWindow{
public:
    SlidingWindow();
    ~SlidingWindow();

    //向滑动窗口添加数据缓冲区
    int AddToBuffer(const SlidWinBuffer& buffer);

    //存储滑动窗口内的所有缓冲区
    std::vector<SlidWinBuffer> sliding_window_buffers_;
    //最后发送的数据包的指针，本质是vector数组的下标
    int last_packet_sent_;
    //最后确认收到的数据包的指针
    int last_acked_packet_;
    //成功发送且已经确认的数据包中最新的序列号
    int send_base_;
    //重复确认计数，用于快速重传机制
    int dup_ack_;
};
}