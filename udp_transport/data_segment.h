#pragma once

#include<cstdint>
#include<cstdlib>

namespace safe_udp{
//这里的关键字为常量表达式，告知编译器某个表达式或者函数在编译期就能计算出确定的值，其结果可以被当作常量来使用
constexpr int MAX_PACKET_SIZE = 1472;  //除去ip头和udp头之后的片段最大长度
constexpr int MAX_DATA_SIZE = 1460;   //数据的最大长度
constexpr int HEADER_LENGTH = 12;    //自定义头部的长度

class DataSegment{
public:
    DataSegment();
    ~DataSegment(){
        if(final_packet_!=NULL){
            free(final_packet_);
        }
    }

    char *SerializeToCharArray();
    void DeserializeToDataSegment(unsigned char *data_segment, int length);

    int seq_number_;  //序列号：在建立连接时由计算机生成的随机数作为其初始值，通过 SYN 包传给接收端主机，每发送一次数据，就「累加」一次该「数据字节数」的大小。用来解决网络包乱序问题
    int ack_number_;  //确认应答号：指下一次「期望」收到的数据包的序列号，发送端收到这个确认应答以后可以认为在这个序号以前的数据都已经被正常接收。用来解决丢包的问题
    bool ack_flag_;  //ACK：该位为 1 时，「确认应答」的字段变为有效，TCP 规定除了最初建立连接时的 SYN 包之外该位必须设置为 1
    bool fin_flag_;   //该位为 1 时，表示今后不会再有数据发送，希望断开连接
    uint16_t length_;
    char *data_ = NULL;

private:
    uint32_t convert_to_uint32(unsigned char *buffer,int start_index);
    bool convert_to_bool(unsigned char *buffer,int index);
    uint16_t convert_to_uint16(unsigned char *buffer,int start_index);
    char *final_packet_ = NULL;
};
}

////重点，这里的ack也好seq也罢是针对整个包而言，但是以字节为单位，详情见输出