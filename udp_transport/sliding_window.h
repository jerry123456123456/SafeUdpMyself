#pragma once

#include <vector>
#include<cstddef>
#include "chain_buffer.h" // 引入 chainbuffer 头文件

namespace safe_udp {
// 定义 ChainBufferType 结构体
struct ChainBufferType {
    // 这里可以根据 chainbuffer 的实际需求添加成员变量
    // 例如，假设 chainbuffer 存储的数据和长度
    char* data;
    size_t length;
    // 可以添加构造函数、析构函数等，根据实际情况完善
    ChainBufferType() : data(nullptr), length(0) {}
    ~ChainBufferType() {
        if (data) {
            delete[] data;
        }
    }
};

class SlidingWindow {
 public:
  SlidingWindow();
  ~SlidingWindow();

  int AddToBuffer(const ChainBufferType& buffer);

  std::vector<ChainBufferType> sliding_window_buffers_;
  int last_packet_sent_;
  int last_acked_packet_;
  // 成功发送且已经确认的数据包中最小的序列号
  int send_base_;
  int dup_ack_;
};
}  // namespace safe_udp