//g++ -o tcp_client tcp_client.cpp -lglog

#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <glog/logging.h>

const int MAX_DATA_SIZE = 1024;

class TcpClient {
public:
    TcpClient() : sockfd_(0) {}

    void CreateSocketAndServerConnection(const std::string &server_address, const std::string &port) {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            LOG(ERROR) << "Failed to create socket !!!";
            return;
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(atoi(port.c_str()));
        if (inet_pton(AF_INET, server_address.c_str(), &server_addr.sin_addr) <= 0) {
            LOG(ERROR) << "Invalid address or address not supported !!!";
            close(sockfd_);
            return;
        }

        if (connect(sockfd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            LOG(ERROR) << "Connection failed !!!";
            close(sockfd_);
            return;
        }
    }

    void ReceiveFile(const std::string &file_name) {
        std::ofstream file;
        std::string file_path = "/work/files/client_files/" + file_name;
        file.open(file_path.c_str(), std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            LOG(ERROR) << "Failed to open file for writing !!!";
            return;
        }

        char buffer[MAX_DATA_SIZE];
        ssize_t received;
        while ((received = recv(sockfd_, buffer, MAX_DATA_SIZE, 0)) > 0) {
            file.write(buffer, received);
        }

        file.close();
        close(sockfd_);
    }

private:
    int sockfd_;
};


int main(int argc, char *argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_minloglevel = google::GLOG_INFO;

    if (argc != 4) {
        LOG(ERROR) << "Usage: " << argv[0] << " <server_ip> <server_port> <file_name>";
        return 1;
    }

    std::string server_ip(argv[1]);
    std::string port_num(argv[2]);
    std::string file_name(argv[3]);

    TcpClient client;
    client.CreateSocketAndServerConnection(server_ip, port_num);
    client.ReceiveFile(file_name);

    return 0;
}