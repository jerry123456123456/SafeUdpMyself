#include "sliding_window.h"

namespace safe_udp{
SlidingWindow::SlidingWindow(){
    last_acked_packet_ = -1;
    last_packet_sent_ = -1;
    send_base_ = -1;
    dup_ack_ = 0;
}

SlidingWindow::~SlidingWindow(){}

int SlidingWindow::AddToBuffer(const SlidWinBuffer&buffer){
    sliding_window_buffers_.push_back(buffer);
    //返回的是索引
    return (sliding_window_buffers_.size() -1);
}

}