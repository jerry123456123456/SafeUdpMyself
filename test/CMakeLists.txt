add_executable(server server.cpp)
target_include_directories(server PUBLIC
  ../udp_transport
)

# 使用关键字签名链接 udp_transport 库
target_link_libraries(server PRIVATE udp_transport)

add_executable(client client.cpp)
target_include_directories(client PUBLIC
  ../udp_transport
)

# 使用关键字签名链接 udp_transport 库
target_link_libraries(client PRIVATE udp_transport)

install(TARGETS  server  client DESTINATION  ${PROJECT_BINARY_DIR}/bin)