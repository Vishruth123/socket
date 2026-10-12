#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace std;

int main() {

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(6500);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(sockfd, (struct sockaddr*)&serverAddr,
         sizeof(serverAddr));

    bool rec=true;

    while(rec){cout << "Waiting for message..." << endl;

    char buffer[1024];

    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);

    int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                     (struct sockaddr*)&clientAddr,
                     &clientLen);

    buffer[n] = '\0';

    cout << "Received: " << buffer << endl;
    if (strcmp(buffer, "End") == 0)
    rec = false;
}

    close(sockfd);

    return 0;
}