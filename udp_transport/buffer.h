#pragma once

#include<sys/time.h>
#include<time.h>

namespace safe_udp{
    //这里之所以没有char*这样的数据，是因为在文件中,传输的是文件
class SlidWinBuffer{
public:
    SlidWinBuffer(){}
    ~SlidWinBuffer(){}
    
    //该buffer的第一个字节的索引值,也就是当前数据包的第一个字节在文件中的位置
    int first_byte_;
    //该buffer的数据大小
    int data_length_;
    //该buffer的序列号,以字节为单位
    int seq_num_;
    //记录该buffer发送的时间戳，为了记录超时重传时间
    struct timeval time_sent_;
};
}