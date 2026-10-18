#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
using namespace std;

bool sendAll(int socketFD, char* buffer, int length) {
    int sent = 0;
    while (sent < length) {
        int result = send(socketFD, buffer + sent, length - sent, 0);
        if (result <= 0) return false;
        sent += result;
    }
    return true;
}

bool receiveBytes(int socketFD, char* buffer, int length) {
    int received = 0;
    while (received < length) {
        int result = recv(socketFD, buffer + received, length - received, 0);
        if (result <= 0) return false;  
        received += result;
    }
    return true;
}

bool handleLinkStateMessage(int tcpSocket) {
    char header[2];
    if (!receiveBytes(tcpSocket, header, 2) || header[0] != 2)
        return false;

    int tupleCount = (unsigned char)header[1];
    if (tupleCount > 26) return false;

    char nodeIDs[26];
    int costs[26];

    for (int i = 0; i < tupleCount; i++) {
        char tuple[11];
        if (!receiveBytes(tcpSocket, tuple, 11)) return false;

        nodeIDs[i] = tuple[0];

        unsigned int networkCost;
        memcpy(&networkCost, tuple + 7, 4);
        costs[i] = ntohl(networkCost);
    }

    char myNodeID = '?';
    for (int i = 0; i < tupleCount; i++)
        if (costs[i] == 0) myNodeID = nodeIDs[i];

    cout << "My node: " << myNodeID << "\nNeighbourhood: ";

    for (int i = 0; i < tupleCount; i++) {
        cout << nodeIDs[i] << "=" << costs[i];
        if (i < tupleCount - 1) cout << ", ";
    }

    cout << endl;
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 4) return 1;

    int udpPort = atoi(argv[3]);

    int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in myUDPAddress{};
    myUDPAddress.sin_family = AF_INET;
    myUDPAddress.sin_port = htons(udpPort);
    inet_pton(AF_INET, argv[2], &myUDPAddress.sin_addr);

    if (bind(udpSocket, (sockaddr*)&myUDPAddress, sizeof(myUDPAddress)) < 0)
        return 1;

    int tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in oracleAddress{};
    oracleAddress.sin_family = AF_INET;
    oracleAddress.sin_port = htons(5000);
    inet_pton(AF_INET, argv[1], &oracleAddress.sin_addr);

    if (connect(tcpSocket, (sockaddr*)&oracleAddress, sizeof(oracleAddress)) < 0)
        return 1;

    char message[7];
    message[0] = 1;
    inet_pton(AF_INET, argv[2], message + 1);

    unsigned short networkPort = htons(udpPort);
    memcpy(message + 5, &networkPort, 2);

    if (!sendAll(tcpSocket, message, 7)) return 1;

    cout << "Connected to ON. Waiting for LINK-STATE...\n";

    while (handleLinkStateMessage(tcpSocket)) {}

    close(tcpSocket);
    close(udpSocket);
    return 0;
}
