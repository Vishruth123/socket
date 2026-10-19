#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
using namespace std;

struct Neighbour {
    char id; sockaddr_in address;
    int cost;
};

bool sendAll(int socketFD, char* buffer, int length) {
    int sent = 0;
    while (sent < length) {
        int result = send(socketFD, buffer + sent, length - sent, 0);
        if (result <= 0) return false;
        sent += result;
    }
    return true;
}

void printGraph(int graph[26][26], bool known[26]) {
    cout << "Graph at this point in time: \n";
    for (int i = 0; i < 26; i++) {
        if (! known[i]) continue;
        cout << char ('A' + i) << ": ";
        bool first = true;
        for (int j = 0; j < 26; j++) {
            if ( i != j && graph [i][j] > 0) {
                if (! first) cout << ", ";
                cout << char('A' + j) << "=" << graph[i][j];
                first = false;
            }
        }
        cout << "\n";
    }
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

bool handleLinkStateMessage(int tcpSocket, char & myID, Neighbour neighbours[], int & neighbourCount) {
    char countByte;
    if (!receiveBytes(tcpSocket, & countByte, 1))
        return false;

    int tupleCount = (unsigned char)countByte;
    if (tupleCount < 1 || tupleCount > 26) return false;

    Neighbour newNeighbours[26];
    int newCount = 0;
    char newID = '?';

    for (int i = 0; i < tupleCount; i++) {
        char tuple[11];
        if (!receiveBytes(tcpSocket, tuple, 11)) return false;

        if(tuple[0] < 'A' || tuple[0] > 'Z') return false;

        unsigned int networkCost;
        memcpy(&networkCost, tuple + 7, 4);
        int cost = ntohl(networkCost);

        if (cost == 0) {
            if (newID != '?') return false;
            newID = tuple[0];
        }
        else {
            newNeighbours[newCount].id = tuple[0];
            newNeighbours[newCount].address = {};
            newNeighbours[newCount].address.sin_family = AF_INET;

            memcpy(&newNeighbours[newCount].address.sin_addr, tuple + 1, 4);
            memcpy(&newNeighbours[newCount].address.sin_port, tuple + 5, 2);

            newNeighbours[newCount].cost = cost;
            newCount++;
        }
    }

    if (newID < 'A' || newID > 'Z') {
        return false;
    }
    myID = newID;
    neighbourCount = newCount;

    for (int i = 0; i < newCount; i++)
        neighbours[i] = newNeighbours[i];

    cout << "My node: " << myID << "\nNeighbourhood: " << myID << "=0";

    for (int i = 0; i < neighbourCount; i++) {
        cout << ", " << neighbours[i].id << "=" << neighbours[i].cost;
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

    char message[6];
    inet_pton(AF_INET, argv[2], message);

    unsigned short networkPort = htons(udpPort);
    memcpy(message + 4, &networkPort, 2);

    if (!sendAll(tcpSocket, message, 6)) return 1;

    cout << "Connected to ON. Waiting for LINK-STATE...\n";

    char myID = '?';
    Neighbour neighbours[26];
    int neighbourCount = 0;

    while (handleLinkStateMessage(tcpSocket, myID, neighbours, neighbourCount)) {}

    close(tcpSocket);
    close(udpSocket);
    return 0;
}
