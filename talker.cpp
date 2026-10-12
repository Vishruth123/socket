#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

int main() {

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(6500);

    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    string message;
    cin >> message;

    sendto(sockfd, message.c_str(), message.length(), 0,
           (struct sockaddr*)&serverAddr,
           sizeof(serverAddr));

    cout << "Message sent!" << endl;

    close(sockfd);

    return 0;
}