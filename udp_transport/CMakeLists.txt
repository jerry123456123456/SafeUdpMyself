set(file
  data_segment.cpp
  packet_statistics.cpp
  sliding_window.cpp
  udp_server.cpp
  udp_client.cpp
)

add_library(udp_transport SHARED ${file})

# 链接 glog 库
target_link_libraries(udp_transport PRIVATE glog)

# 链接 chainbuffer 库
target_link_libraries(udp_transport PRIVATE chainbuffer)

# 安装 udp_transport 库
install(TARGETS  udp_transport DESTINATION  ${PROJECT_BINARY_DIR}/lib)