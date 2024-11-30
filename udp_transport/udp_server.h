#pragma once

#include<netinet/in.h>
#include<unistd.h>
#include<fstream>
#include<iostream>
#include<memory>
#include<string>

#include"data_segment.h"
#include"packet_statistics.h"
#include"sliding_window.h"

namespace safe_udp{
class UdpServer{
public:
    UdpServer();
    ~UdpServer(){
        close(sockfd_);
        file_.close();
    }

    char *GetRequest(int client_sockfd);  //获取客户端请求
    bool OpenFile(const std::string &file_name);  //打开文件
    void StartFileTransfer();   //开始文件传输
    void SendError();  //发送错误信息

    int rwnd_;   //接收窗口大小
    int cwnd_;   //拥塞窗口的大小
    int ssthresh_;  //慢启动门限
    int start_byte_;  //起始字节
    bool is_slow_start_;   //是否处于慢启动状态
    bool is_cong_avd_;     //是否处于拥塞避免状态
    bool is_fast_recovery_;  //是否处于快速恢复状态
    int StartServer(int port);   //启动服务器

private:
    std::unique_ptr<SlidingWindow> sliding_window_;   //滑动窗口对象
    std::unique_ptr<PacketStatistics> packet_statistics_;  //数据包统计对象

    int sockfd_;    //套接字描述符
    std::fstream file_;   //文件流对象
    struct sockaddr_in cli_address_;   //客户端地址结构体
    int initial_seq_number_;  //初始序号
    int file_length_;  //文件长度
    double smoothed_rtt_;    //平滑往返时间
    double dev_rtt_;   //往返时间偏差
    double smoothed_timeout_;   //平滑超时时间

    void send();  //发送数据包
   
    void send_packet(int seq_number,int start_byte);    //发送数据包
    void calculate_rtt_and_time(struct timeval start_time,   //计算往返时间
                              struct timeval end_time);
    void retransmit_segment(int index_number);   //重传数据段
    void read_file_and_send(bool fin_flag, int start_byte, int end_byte);  //读文件并发送数据
    void send_data_segment(DataSegment *data_segment);   //发送数据段
    void wait_for_ack();   //等待确认
};
}
