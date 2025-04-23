// g++ -o tcp_server tcp_server.cpp -lglog

#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <glog/logging.h>

const int MAX_DATA_SIZE = 1024;

class TcpServer {
public:
    TcpServer() : sockfd_(0), file_length_(0) {}

    int StartServer(int port) {
        LOG(INFO) << "Starting the TCP server... port: " << port;
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            LOG(ERROR) << "Failed to create socket !!!";
            return -1;
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(sockfd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            LOG(ERROR) << "Binding error !!!";
            close(sockfd_);
            return -1;
        }

        if (listen(sockfd_, 5) < 0) {
            LOG(ERROR) << "Listen error !!!";
            close(sockfd_);
            return -1;
        }

        LOG(INFO) << "Started successfully";
        return sockfd_;
    }

    bool OpenFile(const std::string &file_name) {
        LOG(INFO) << "Opening the file " << file_name;
        file_.open(file_name.c_str(), std::ios::in | std::ios::binary);
        if (!file_.is_open()) {
            LOG(INFO) << "File: " << file_name << " opening failed";
            return false;
        } else {
            LOG(INFO) << "File: " << file_name << " opening success";
            file_.seekg(0, std::ios::end);
            file_length_ = file_.tellg();
            file_.seekg(0, std::ios::beg);
            return true;
        }
    }

    void StartFileTransfer() {
        LOG(INFO) << "Starting the file transfer ";
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_sockfd = accept(sockfd_, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sockfd < 0) {
            LOG(ERROR) << "Accept error !!!";
            return;
        }

        struct timeval start_time, end_time;
        gettimeofday(&start_time, NULL);

        char buffer[MAX_DATA_SIZE];
        while (file_.read(buffer, MAX_DATA_SIZE)) {
            ssize_t sent = send(client_sockfd, buffer, MAX_DATA_SIZE, 0);
            if (sent < 0) {
                LOG(ERROR) << "Send error !!!";
                break;
            }
        }
        ssize_t remaining = file_.gcount();
        if (remaining > 0) {
            ssize_t sent = send(client_sockfd, buffer, remaining, 0);
            if (sent < 0) {
                LOG(ERROR) << "Send error !!!";
            }
        }

        gettimeofday(&end_time, NULL);
        int64_t total_time = (end_time.tv_sec * 1000000 + end_time.tv_usec) - (start_time.tv_sec * 1000000 + start_time.tv_usec);

        LOG(INFO) << "\n";
        LOG(INFO) << "========================================";
        LOG(INFO) << "Total Time: " << (float)total_time / 1000000 << " secs";
        LOG(INFO) << "========================================";

        close(client_sockfd);
        file_.close();
    }

private:
    int sockfd_;
    std::ifstream file_;
    int file_length_;
};


int main(int argc, char *argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_minloglevel = google::GLOG_INFO;

    if (argc != 3) {
        LOG(ERROR) << "Usage: " << argv[0] << " <port> <file_name>";
        return 1;
    }

    int port = atoi(argv[1]);
    std::string file_name(argv[2]);

    TcpServer server;
    if (server.StartServer(port) < 0) {
        return 1;
    }

    if (!server.OpenFile(file_name)) {
        return 1;
    }

    server.StartFileTransfer();

    return 0;
}