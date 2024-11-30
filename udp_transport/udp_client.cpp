#include "udp_client.h"

#include <netdb.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include <glog/logging.h>

#include "data_segment.h"


namespace safe_udp{
UdpClient::UdpClient(){
    last_in_order_packet_ = -1;
    last_packet_received_ = -1;
    fin_flag_received_ = false;
}

void UdpClient::SendFileRequest(const std::string &file_name){
    int n;
    int next_seq_expected;
    int segments_in_between = 0;
    initial_seq_number_ = 67;
    if(receiver_window_ == 0){
        receiver_window_ = 100;
    }

    unsigned char *buffer = (unsigned char *)calloc(MAX_PACKET_SIZE,sizeof(unsigned char));
    LOG(INFO) << "server_add::" << server_address_.sin_addr.s_addr;
    LOG(INFO) << "server_add_port::" << server_address_.sin_port;
    LOG(INFO) << "server_add_family::" << server_address_.sin_family;

    n = sendto(sockfd_,file_name.c_str(),file_name.size(),0,(struct sockaddr *)&server_address_,sizeof(struct sockaddr_in));
    if (n < 0) {
        LOG(ERROR) << "Failed to write to socket !!!";
    }
    memset(buffer, 0, MAX_PACKET_SIZE);

    std::fstream file;
    std::string file_path = std::string(CLIENT_FILE_PATH) + file_name;
    //以写的方式打开文件，不存在就创建
    file.open(file_path.c_str(),std::ios::out);

    while((n = recvfrom(sockfd_,buffer,MAX_PACKET_SIZE,0,NULL,NULL) > 0)){
        char buffer2[20];
        memcpy(buffer2,buffer,20);
        if (strstr("FILE NOT FOUND", buffer2) != NULL) {
        LOG(ERROR) << "File not found !!!";
        return;
        }

        std::unique_ptr<DataSegment> data_segment = std::make_unique<DataSegment>();
        data_segment->DeserializeToDataSegment(buffer, n);

        LOG(INFO) << "packet received with seq_number_:"
              << data_segment->seq_number_;

        //模拟丢包
        if(is_packet_drop_ && rand() % 100 < prob_value_){   //这个prob_value_是一个传入参数，在0~100之间
            //随机的部分不处理，模拟丢包
            LOG(INFO) << "Dropping this packet with seq "
                    << data_segment->seq_number_;
            continue;
        }

        //模拟时延
        if(is_delay_ && rand() % 100 < prob_value_){
            //适当睡眠一定时间
            int sleep_time = (rand() % 10) * 1000;
            LOG(INFO) << "Delaying this packet with seq " << data_segment->seq_number_
                        << " for " << sleep_time << "us";
            usleep(sleep_time);
        }

        if(last_in_order_packet_ == -1){
            //如果第一个包，就是约定好的seq
            next_seq_expected = initial_seq_number_;
        }else{
            //否则，就是最后收到的包的seq再加上它的长度，也就是下一个期待收到的seq
            next_seq_expected = data_segments_[last_in_order_packet_].seq_number_ +
                                data_segments_[last_in_order_packet_].length_;
        }

        /*
        而判断 !data_segment->fin_flag_ 主要是为了区分对待普通的旧数据包和带有文件结束标志（fin_flag_）的旧数据包。
        文件结束标志的数据包有着特殊的意义，它代表着整个文件传输即将结束或者已经结束，对于这种带有特殊标志的数据包，
        不能简单地按照普通旧数据包的处理逻辑仅仅发送一个常规的确认就跳过
        */
        if(next_seq_expected > data_segment->seq_number_ && !data_segment->fin_flag_){
            //已经收到过了
            send_ack(next_seq_expected);
            continue;
        }

        //到这里data_segment->seq_number - next_seq_expected为正，而相减除以包大小就是相差的索引个数
        segments_in_between = (data_segment->seq_number_ - next_seq_expected) / MAX_DATA_SIZE;
        //当前收到的索引
        int this_segment_index = last_in_order_packet_ + segments_in_between + 1;;

        //超过就收窗口大小了，直接丢掉
        if (this_segment_index - last_in_order_packet_ > receiver_window_) {
            LOG(INFO) << "Packet dropped " << this_segment_index;
            // Drop the packet, if it exceeds receiver window
            continue;
        }

        if(data_segment->fin_flag_){
            LOG(INFO) << "Fin flag received !!!";
            fin_flag_received_ = true;
        }

        //插入进去
        insert(this_segment_index,*data_segment);

        //同时文件写入
        for(int i= last_in_order_packet_ + 1;i <= last_packet_received_; i++){
            if(data_segments_[i].seq_number_ != -1){
                if(file.is_open()){
                    file << data_segments_[i].data_;
                    last_in_order_packet_ = i;
                }
            }else{
                break;
            }
        }

        // 如果已经接收到 fin_flag_ 且所有数据包都处理完毕，则跳出循环
        if (fin_flag_received_ && last_in_order_packet_ == last_packet_received_) {
            break;
        }

        send_ack(data_segments_[last_in_order_packet_].seq_number_ +
             data_segments_[last_in_order_packet_].length_);
        memset(buffer, 0, MAX_PACKET_SIZE);  
    }
    free(buffer);
    file.close();
}

int UdpClient::add_to_data_segment_vector(const DataSegment &data_segment){
    data_segments_.push_back(data_segment);
    return data_segments_.size() - 1;
}

void UdpClient::send_ack(int ackNumber){
    LOG(INFO) << "Sending an ack :" << ackNumber;
    int n = 0;
    DataSegment *ack_segment = new DataSegment();
    ack_segment->ack_flag_ = true;
    ack_segment->ack_number_ = ackNumber;
    ack_segment->fin_flag_ = false;
    ack_segment->length_ = 0;
    ack_segment->seq_number_ = 0;

    char *data = ack_segment->SerializeToCharArray();
    n = sendto(sockfd_,data,MAX_PACKET_SIZE,0,(struct sockaddr *)&server_address_,sizeof(struct sockaddr_in));

    if(n < 0){
        LOG(INFO) << "Sending ack failed !!!";
    }

    free(data);
}

//我这里使用更现代的方式代替原来的
void UdpClient::CreateSocketAndServerConnection(const std::string &server_address, const std::string &port){
    int port_num;
    int sfd;
    try{
        port_num = std::stoi(port);   //和atoi的区别是接受的参数是string不是char *
    }catch(const std::invalid_argument &e){
        //当传递给 std::stoi 的字符串不是一个合法的表示整数的格式
        std::cerr << "Invalid port number format: " << e.what() << std::endl;
        throw;  // 重新抛出异常，让调用者进一步处理
    }catch (const std::out_of_range &e) {
        //捕获当 std::stoi 转换得到的整数超出了 int 类型所能表示的范围时抛出的异常
        std::cerr << "Port number out of range: " << e.what() << std::endl;
        throw;
    }
    
    struct addrinfo hints = {}, *server_info = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    hints.ai_flags = 0;
    //这一步其实是最终都存到server_info这个结构体中
    int ret = getaddrinfo(server_address.c_str(),port.c_str(),&hints,&server_info);
    if (ret != 0) {
        std::cerr << "Failed to get address info: " << gai_strerror(ret) << std::endl;
        throw std::runtime_error("Failed to resolve server address");
    }

    sfd = socket(server_info->ai_family,server_info->ai_socktype,server_info->ai_protocol);
    if (sfd < 0) {
        std::cerr << "Failed to create socket!" << std::endl;
        freeaddrinfo(server_info);  // 释放 addrinfo 结构
        throw std::runtime_error("Failed to create socket");
    }

    //将服务器信息拷贝到server_address中
    if(server_info->ai_family == AF_INET){
        sockaddr_in *addr_in = reinterpret_cast<sockaddr_in *>(server_info->ai_addr);
        this->server_address_ = *addr_in;
    }

    //存储socket文件描述符
    sockfd_ = sfd;

    freeaddrinfo(server_info);
}

void UdpClient::insert(int index,const DataSegment &data_segment){
    if(index > last_packet_received_){
        //这个时候，这个数据包之前没有存储过
        for(int i = last_packet_received_ + 1; i <= index; i++){
            //从最后存好的下一个开始遍历直到找到对应的位置，因为收到包的顺序不一定，可能大的序号先收到
            if(i == index){
                //找到位置了，插进去
                data_segments_.push_back(data_segment);
            }else{
                //其他位置拿空包占位置
                DataSegment data_segment1;
                data_segments_.push_back(data_segment1);
            }   
            //更新最后一个
            last_packet_received_ = index;
        }
    }else{
        //收到的是存储过的，那么直接覆盖
        data_segments_[index] = data_segment;
    }
}
}
