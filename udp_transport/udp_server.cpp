#include"udp_server.h"
#include <cstring>
#include<sys/epoll.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<sys/types.h>
#include<time.h>
#include<algorithm>
#include<cmath>
#include<sstream>
#include<vector>
#include<glog/logging.h>

namespace safe_udp{
UdpServer::UdpServer(){
    sliding_window_ = std::make_unique<SlidingWindow>();
    packet_statistics_ = std::make_unique<PacketStatistics>();

    sockfd_ = 0;
    //根据测试给出的这些值
    smoothed_rtt_ = 20000;
    smoothed_timeout_ = 30000;
    dev_rtt_ =0;

    //67是随机的
    initial_seq_number_ = 67;
    start_byte_ = 0;

    ssthresh_ = 128;
    cwnd_ = 1;

    is_slow_start_ = true;
    is_cong_avd_ = false;
    is_fast_recovery_ = false;
}

int UdpServer::StartServer(int port){
    int sfd;
    struct sockaddr_in server_addr;
    LOG(INFO) << "Starting the webserver... port: " << port;
    sfd = socket(AF_INET,SOCK_DGRAM,0);

    if(sfd < 0){
        LOG(ERROR) << " Failed to socket !!!";
        exit(0);
    }

    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);

    if(bind(sfd,(struct sockaddr *)&server_addr,sizeof(server_addr))<0){
        LOG(ERROR) << "binding error !!!";
        exit(0);
    }

    LOG(INFO) << "**Server Bind set to addr: " << server_addr.sin_addr.s_addr;
    LOG(INFO) << "**Server Bind set to port: " << server_addr.sin_port;
    LOG(INFO) << "**Server Bind set to family: " << server_addr.sin_family;
    LOG(INFO) << "Started successfully";
    sockfd_ = sfd;
    return sfd;
}

bool UdpServer::OpenFile(const std::string &file_name){
    LOG(INFO) << "Opening the file " << file_name;
    //std::ios::in表示以输入模式（也就是只读模式）打开文件
    file_.open(file_name.c_str(),std::ios::in);
    if (!this->file_.is_open()) {
        LOG(INFO) << "File: " << file_name << " opening failed";
        return false;
    } else {
        LOG(INFO) << "File: " << file_name << " opening success";
        return true;
    }
}

void UdpServer::StartFileTransfer(){
    LOG(INFO) << "Starting the file_ transfer ";

    file_.seekg(0,std::ios::end);  //先定位到文件末尾
    file_length_ = file_.tellg();  //由于定位在末尾，这个tellg就是整个文件的大小
    file_.seekg(0,std::ios::beg);  //在回到开头准备开始传输

    send();
}

void UdpServer::SendError(){
    std::string error("FILE NOT FOUND");
    sendto(sockfd_,error.c_str(),error.size(),0,(struct sockaddr *)&cli_address_,sizeof(cli_address_));
}

void UdpServer::send(){    //整个函数的逻辑是，每发送一个滑动窗口大小的数据包，等待一次epoll事件
    LOG(INFO) << "Entering Send()";

    int sent_count = 1;   //已经发送的数据分组数量
    int sent_count_limit = 1;  //最大发送数量

    struct timeval process_start_time;
    struct timeval process_end_time;
    gettimeofday(&process_start_time,NULL);

    //创建epoll实例
    int epoll_fd = epoll_create(1);
    if (epoll_fd == -1) {
        LOG(ERROR) << "Error creating epoll instance";
        return;  // 如果创建失败，直接返回，避免后续错误操作
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = sockfd_;   
    //将服务器的套接字文件描述符添加到epoll实例中进行监听
    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,sockfd_,&event) == -1){
        LOG(ERROR) << "Error adding socket to epoll";
        close(epoll_fd);  // 如果添加失败，关闭epoll文件描述符
        return;
    } 

    if(sliding_window_->last_packet_sent_ == -1){   //最后发送的分组包的下标
        //如果是-1就是还没有开始发送
        start_byte_ = 0;
    }

    while(start_byte_ <= file_length_){
        fd_set rfds;  //管理文件描述符集合
        struct timeval tv;
        int res;

        sent_count = 1;
        sent_count_limit = std::min(rwnd_ ,cwnd_);

        LOG(INFO) << "SEND START  !!!!";
        LOG(INFO) << "Before the window rwnd_: " << rwnd_ << " cwnd_: " << cwnd_
                << " window used: "
                << sliding_window_->last_packet_sent_ -
                        sliding_window_->last_acked_packet_;
        //一是判断当前已发送但未确认的数据包数量（通过sliding_window_->last_packet_sent_ - sliding_window_->last_acked_packet_计算）是否小于等于接收窗口和拥塞窗口大小的较小值；
        //二是判断已发送的数据包数量（sent_count）是否小于等于发送数量上限（sent_count_limit）
        while (sliding_window_->last_packet_sent_ -
                   sliding_window_->last_acked_packet_ <=
               std::min(rwnd_, cwnd_) &&
           sent_count <= sent_count_limit) {

            send_packet(start_byte_ + initial_seq_number_, start_byte_);

            //////////////////////////

            if(is_slow_start_){
                packet_statistics_->slow_start_packet_sent_count_++;
            }else if(is_cong_avd_){
                packet_statistics_->cong_avd_packet_sent_count_++;
            }

            // break
            start_byte_ = start_byte_ + MAX_DATA_SIZE;
            if (start_byte_ > file_length_) {
                LOG(INFO) << "No more data left to be sent";
                break;
            }
            sent_count++;
        }

        LOG(INFO) << "SEND END!!!!!";
        //用于存储epoll_wait返回的就绪事件数组
        struct epoll_event events[1024];
        int num_events;

        //设置epoll_wait的超时时间，这里是平滑超时时间
        int timeout = smoothed_timeout_;
        num_events = epoll_wait(epoll_fd,events,1024,timeout);
        LOG(INFO) << "EPOLL START:" << smoothed_timeout_ << "!!!";
        if(num_events == -1){
            LOG(ERROR) << "Error in epoll_wait";
        }else if(num_events > 0){   //有就绪事件
            //遍历所有就绪事件，如果事件来自套接字且为可读，调用wait_for_ack处理返回的ack
            for(int i = 0;i < num_events; ++i){
                if (events[i].data.fd == sockfd_ && (events[i].events & EPOLLIN)) {
                    wait_for_ack();
                    //如果窗口大小超过阈值，从慢启动切换到拥塞避免
                    if(cwnd_ >= ssthresh_){
                        is_cong_avd_ = true;
                        is_slow_start_ = false;
                        cwnd_ = 1;
                        ssthresh_ = 64;
                    }
                    //如果滑动窗口里的数据全部确认，认为网络顺畅，可以增大滑动窗口大小
                    if(sliding_window_->last_acked_packet_ == sliding_window_->last_packet_sent_){
                        if(is_slow_start_){
                            cwnd_ = cwnd_ * 2;
                        }else{
                            cwnd_ = cwnd_ + 1;
                        }
                        break;
                    }
                }
            }
        }else{   //超时情况，拥塞发生，超时重传
            LOG(INFO) << "Timeout occurred";
            ssthresh_ = cwnd_ / 2;
            if (ssthresh_ < 1) {
                ssthresh_ = 1;
            }
            cwnd_ = 1;

            //重新开始慢启动,拥塞发生，不可以在快速恢复了
            if(is_fast_recovery_){
                is_fast_recovery_ = false;
            }
            is_slow_start_ = true;
            is_cong_avd_ = false;

            //重传未确认的数据包
            for(int i = sliding_window_->last_acked_packet_ + 1;i <= sliding_window_->last_packet_sent_; i++){
                int retransmit_start_byte = 0; //初始化重传起始字节
                //如果滑动窗口中至少有一个确认过的包
                if(sliding_window_->last_acked_packet_ != -1){
                    //计算出下一个包的起始字节偏移量
                    retransmit_start_byte = sliding_window_->sliding_window_buffers_[sliding_window_->last_acked_packet_].first_byte_ + MAX_DATA_SIZE;
                }
                LOG(INFO) << "Timeout Retransmit seq number"
                          << retransmit_start_byte + initial_seq_number_;

                retransmit_segment(retransmit_start_byte);
                packet_statistics_->retransmit_count_++;
                LOG(INFO) << "Timeout: retransmission at " << retransmit_start_byte;
            }
        }
    }
    close(epoll_fd);  // 循环结束后，关闭epoll文件描述符
  
    gettimeofday(&process_end_time, NULL);

    int64_t total_time =
        (process_end_time.tv_sec * 1000000 + process_end_time.tv_usec) -
        (process_start_time.tv_sec * 1000000 + process_start_time.tv_usec);

    int total_packet_sent = packet_statistics_->slow_start_packet_sent_count_ +
                            packet_statistics_->cong_avd_packet_sent_count_;
    LOG(INFO) << "\n";
    LOG(INFO) << "========================================";
    LOG(INFO) << "Total Time: " << (float)total_time / pow(10, 6) << " secs";
    LOG(INFO) << "Statistics: 拥塞控制--慢启动: "
                << packet_statistics_->slow_start_packet_sent_count_
                << " 拥塞控制--拥塞避免: "
                << packet_statistics_->cong_avd_packet_sent_count_;
    LOG(INFO) << "Statistics: Slow start: "
                << ((float)packet_statistics_->slow_start_packet_sent_count_ /
                    total_packet_sent) *
                    100
                << "% CongAvd: "
                << ((float)packet_statistics_->cong_avd_packet_sent_count_ /
                    total_packet_sent) *
                    100
                << "%";
    LOG(INFO) << "Statistics: Retransmissions: "
                << packet_statistics_->retransmit_count_;
    LOG(INFO) << "========================================";
}

//seq_number数据包的序列号，start_byte为要发送的数据在整个文件中的那个字节
void UdpServer::send_packet(int seq_number,int start_byte){
    bool lastPacket = false;  //标记当前要发送的数据包是否是最后一个数据包
    int dataLength = 0;  //存储本次要发送的数据的长度

    //每发送一次start_byte就会更新，增加1460
    if(file_length_ <= start_byte + MAX_DATA_SIZE){  //如果文件很大，大于1460，就填满1460,反之只能发送小于1460的数据包
        LOG(INFO) << "Last packet to be sent!!!";
        dataLength = file_length_ - start_byte;
        lastPacket = true;
    }else{
        dataLength = MAX_DATA_SIZE;
    }

    struct timeval time;

    gettimeofday(&time,NULL);
    //说明当前要处理的情况可能是数据包重传或者是对之前已在窗口中但未完全处理完的数据进行时间更新等情况
    if(sliding_window_->last_packet_sent_ != -1 &&   //!=-1说明发过了
            start_byte < sliding_window_->sliding_window_buffers_[sliding_window_->last_packet_sent_].first_byte_){   
        //从最后一个确认的包的下一个开始,小于最后一个发送的包，自增
        for(int i = sliding_window_->last_acked_packet_ + 1;
            i < sliding_window_->last_packet_sent_;i++){
            //找到了对应的数据包
            if (sliding_window_->sliding_window_buffers_[i].first_byte_ ==
          start_byte) {
            sliding_window_->sliding_window_buffers_[i].time_sent_ = time;   //更新时间戳
                break;
            }
        }
    }else{  //这里就理解为新发的包，也就是最后一个包
        SlidWinBuffer slidingWindowBuffer;
        slidingWindowBuffer.first_byte_ = start_byte;
        slidingWindowBuffer.data_length_ = dataLength;
        slidingWindowBuffer.seq_num_ = seq_number;
        struct timeval time;
        gettimeofday(&time,NULL);
        slidingWindowBuffer.time_sent_ = time;
        sliding_window_->last_packet_sent_ = sliding_window_->AddToBuffer(slidingWindowBuffer);
    }
    //是否为最后一个，开始的字节，结束的字节
    read_file_and_send(lastPacket,start_byte,start_byte + dataLength);
} 

void UdpServer::wait_for_ack(){
    unsigned char buffer[MAX_PACKET_SIZE];
    memset(buffer,0,MAX_PACKET_SIZE);
    socklen_t addr_size;
    struct sockaddr_in client_address;
    addr_size = sizeof(client_address);
    int n = 0;
    int ack_number;
    //阻塞等待接受客户端的返回数据
    //n是接收的字节数
    while ((n = recvfrom(sockfd_, buffer, MAX_PACKET_SIZE, 0,
                       (struct sockaddr *)&client_address, &addr_size)) <= 0) {
    };

    DataSegment ack_segment;
    ack_segment.DeserializeToDataSegment(buffer,n);

    //先提取滑动窗口中最后一个已经确认的
    SlidWinBuffer last_packet_acked_buffer = sliding_window_->sliding_window_buffers_[sliding_window_->last_acked_packet_];

    if(ack_segment.ack_flag_){   //只处理ack有效的情况
        //如果期待收到的是之前确认的，说明这是个重复的数据包
        if(ack_segment.ack_number_ == sliding_window_->send_base_){
            LOG(INFO) << "DUP ACK Received: ack_number: " << ack_segment.ack_number_;
            sliding_window_->dup_ack_++;
            //快速重传
            if(sliding_window_->dup_ack_ == 3){
                packet_statistics_->retransmit_count_++;
                LOG(INFO) << "Fast Retransmit seq_number: " << ack_segment.ack_number_;
                //重传,下面两个值相减是第i个字节（正要重传的)
                retransmit_segment(ack_segment.ack_number_ - initial_seq_number_);
                sliding_window_->dup_ack_ = 0;
                if(cwnd_ > 1){
                    cwnd_ = cwnd_ / 2;
                }
                ssthresh_ = cwnd_;
                is_fast_recovery_ = true;
            }

        }else if(ack_segment.ack_number_ > sliding_window_->send_base_){
            //一般的策略是每收到一个新的确认应答（即非重复确认），就适当增加 cwnd 的大小，以此逐步恢复数据发送的速率
            if(is_fast_recovery_){
                cwnd_++;
                is_fast_recovery_ = false;
                is_cong_avd_ = true;
                is_slow_start_ = false;
            }

            sliding_window_->dup_ack_ = 0;
            sliding_window_->send_base_ = ack_segment.ack_number_;
            if (sliding_window_->last_acked_packet_ == -1) {
                sliding_window_->last_acked_packet_ = 0;
                last_packet_acked_buffer =
                    sliding_window_
                        ->sliding_window_buffers_[sliding_window_->last_acked_packet_];
            }
            ack_number = last_packet_acked_buffer.seq_num_ +
                        last_packet_acked_buffer.data_length_;

            while (ack_number < ack_segment.ack_number_) {
                sliding_window_->last_acked_packet_++;
                last_packet_acked_buffer =
                    sliding_window_
                        ->sliding_window_buffers_[sliding_window_->last_acked_packet_];
                ack_number = last_packet_acked_buffer.seq_num_ +
                            last_packet_acked_buffer.data_length_;
            }

            struct timeval startTime = last_packet_acked_buffer.time_sent_;
            struct timeval endTime;
            gettimeofday(&endTime, NULL);

            // LOG(INFO) << "seq num of last_acked "
            //           << last_packet_acked_buffer.seq_num_;
            calculate_rtt_and_time(startTime, endTime);
        }
    }
}

void UdpServer::calculate_rtt_and_time(struct timeval start_time,
                                       struct timeval end_time) {
  if (start_time.tv_sec == 0 && start_time.tv_usec == 0) {
    return;
  }
  long sample_rtt = (end_time.tv_sec * 1000000 + end_time.tv_usec) -
                    (start_time.tv_sec * 1000000 + start_time.tv_usec);

  smoothed_rtt_ = smoothed_rtt_ + 0.125 * (sample_rtt - smoothed_rtt_);

  dev_rtt_ = 0.75 * dev_rtt_ + 0.25 * (abs(smoothed_rtt_ - sample_rtt));
  smoothed_timeout_ = smoothed_rtt_ + 4 * dev_rtt_;

  if (smoothed_timeout_ > 1000000) {
    smoothed_timeout_ = rand() % 30000;
  }
}

void UdpServer::retransmit_segment(int index_number){
    for(int i = sliding_window_->last_acked_packet_ + 1;i < sliding_window_->last_packet_sent_;i++){
        //找到更新一下时间就可以重发了
        if (sliding_window_->sliding_window_buffers_[i].first_byte_ ==
        index_number) {
            struct timeval time;
            gettimeofday(&time, NULL);
            sliding_window_->sliding_window_buffers_[i].time_sent_ = time;
            break;
        }
    }
    read_file_and_send(false, index_number, index_number + MAX_DATA_SIZE);
}

void UdpServer::read_file_and_send(bool fin_flag,int start_byte,int end_byte){
    int datalength = end_byte - start_byte;
    if(file_length_ - start_byte <= datalength){  //最后一个包
        datalength = file_length_ - start_byte;
        fin_flag = true;
    }
    char *fileData = reinterpret_cast<char *>(calloc(datalength,sizeof(char)));
    if (!file_.is_open()) {
        LOG(ERROR) << "File open failed !!!";
        return;
    }
    file_.seekg(start_byte);
    file_.read(fileData, datalength);

    DataSegment *data_segment = new DataSegment();
    data_segment->seq_number_ = start_byte + initial_seq_number_;
    data_segment->ack_number_ = 0;   ////发送方无需指定ack
    data_segment->ack_flag_ = false; ////
    data_segment->fin_flag_ = fin_flag;
    data_segment->length_ = datalength;
    data_segment->data_ = fileData;

    send_data_segment(data_segment);
    LOG(INFO) << "Packet sent:seq number: " << data_segment->seq_number_;

    free(fileData);
    free(data_segment);
}

//监听请求
char *UdpServer::GetRequest(int client_sockfd){
    char *buffer = reinterpret_cast<char *>(calloc(MAX_PACKET_SIZE,sizeof(char)));
    struct sockaddr_in client_address;
    socklen_t addr_size;
    memset(buffer,0,MAX_PACKET_SIZE);
    addr_size = sizeof(client_address);

    //阻塞监听
    while(recvfrom(client_sockfd,buffer,MAX_PACKET_SIZE,0,(struct sockaddr *)&client_address,&addr_size) <= 0);
    LOG(INFO) << "***Request received is: " << buffer;
    cli_address_ = client_address;

    return buffer;
}

void UdpServer::send_data_segment(DataSegment *data_segment){
    char *datagramChars = data_segment->SerializeToCharArray();
    sendto(sockfd_, datagramChars, MAX_PACKET_SIZE, 0,
         (struct sockaddr *)&cli_address_, sizeof(cli_address_));
    free(datagramChars);
}
}

