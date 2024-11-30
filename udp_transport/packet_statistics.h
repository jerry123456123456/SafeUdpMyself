#pragma once
//统计数据包
namespace safe_udp{
class PacketStatistics{
public:
    PacketStatistics();
    virtual ~PacketStatistics();

    int slow_start_packet_sent_count_;//慢启动阶段所发送出去的数据包的数量
    int cong_avd_packet_sent_count_;  //拥塞避免阶段发送出去的数据包数量
    int slow_start_packet_rx_count_;  //慢启动阶段接收到的数据包数量
    int cong_avd_packet_rx_count_;    //拥塞避免阶段接收到的数据包数量
    int retransmit_count_;            //表示在整个网络传输过程中，数据包重传的次数
};
}