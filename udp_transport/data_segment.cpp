#include"data_segment.h"
#include<stdlib.h>
#include<string.h>
#include<iostream>
#include<string>

namespace safe_udp{

DataSegment::DataSegment(){
    ack_number_ = -1;
    seq_number_ = -1;
    length_ = -1;
}

//将整个用户定义的1476字节的数据赋值给final_packet_
char *DataSegment::SerializeToCharArray(){
    if(final_packet_!=NULL){
        memset(final_packet_,0,MAX_PACKET_SIZE);
    }else{
        //reinterpret_cast 是 C++ 中的一种强制类型转换操作符
        final_packet_ = reinterpret_cast<char*>(calloc(MAX_PACKET_SIZE,sizeof(char)));
        if(final_packet_ == NULL){
            return NULL;
        }
    }

  memcpy(final_packet_, &seq_number_, sizeof(seq_number_));

  memcpy(final_packet_ + 4, &ack_number_, sizeof(ack_number_));

  memcpy((final_packet_ + 8), &ack_flag_, 1);

  memcpy((final_packet_ + 9), &fin_flag_, 1);

  memcpy((final_packet_ + 10), &length_, sizeof(length_));

  memcpy((final_packet_ + 12), data_, length_);

  return final_packet_;
}

//length是data的长度
void DataSegment::DeserializeToDataSegment(unsigned char *data_segment,
                                           int length) {
  seq_number_ = convert_to_uint32(data_segment, 0);
  ack_number_ = convert_to_uint32(data_segment, 4);
  ack_flag_ = convert_to_bool(data_segment, 8);
  fin_flag_ = convert_to_bool(data_segment, 9);
  length_ = convert_to_uint16(data_segment, 10);

  data_ = reinterpret_cast<char *>(calloc(length + 1, sizeof(char)));
  if (data_ == nullptr) {
    return;
  }
  memcpy(data_, data_segment + HEADER_LENGTH, length);
  *(data_ + length) = '\0';
}

//这个函数的目的是从指定位置以小端字节序提取4个字节
uint32_t DataSegment::convert_to_uint32(unsigned char *buffer,int start_index){
    uint32_t uint32_value = 
       (buffer[start_index + 3] << 24) | (buffer[start_index + 2] << 16) |
       (buffer[start_index + 1] << 8) | (buffer[start_index]);
    return uint32_value;
}

//这个函数的目的是从指定位置以小端字节序提取2个字节
uint16_t DataSegment::convert_to_uint16(unsigned char *buffer,
                                        int start_index) {
  uint16_t uint16_value =
      (buffer[start_index + 1] << 8) | (buffer[start_index]);
  return uint16_value;
}

//这个函数的目的是从指定位置以小端字节序提取1个字节
bool DataSegment::convert_to_bool(unsigned char *buffer, int index) {
  bool bool_value = buffer[index];
  return bool_value;
}
}