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

int nodeIndex(char id){ return id - 'A';}

void updateGraph(char id, const Neighbour neighbours[], int count, int graph[26][26], bool known[26]){
    int row = nodeIndex(id);
    for (int i = 0; i < 26; i++) {
        graph[row][i] = -1;
        graph[i][row] = -1;
    }
    graph[row][row] = 0;
    for (int i = 0; i < count; i++) {
        int neighbour = nodeIndex(neighbours[i].id);
        graph[row][neighbour] = neighbours[i].cost;
        graph[neighbour][row] = neighbours[i].cost;
    }
    known[row] = true;
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

bool handleLinkStateMessage(int tcpSocket, char & myID, Neighbour neighbours[], int & neighbourCount, int graph[26][26], bool known[26]) {
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

    updateGraph(myID, neighbours, neighbourCount, graph, known);

    cout << "My node: " << myID << "\nNeighbourhood: " << myID << "=0";

    for (int i = 0; i < neighbourCount; i++) {
        cout << ", " << neighbours[i].id << "=" << neighbours[i].cost;
    }

    cout << endl;
    printGraph(graph, known);
    return true;
}

bool sendOwnLSP(int udpSocket, char myID, const Neighbour neighbours[], int neighbourCount, unsigned int& sequenceNumber) {
    char message[6 + 5 * 26]; int position = 0;

    sequenceNumber++; // increment every time called

    message[position] = myID;
    position++;

    unsigned int networkSequence = htonl(sequenceNumber);
    memcpy(message + position, & networkSequence, 4);
    position += 4;

    message[position++] = (unsigned char)neighbourCount; 

    for (int i = 0; i < neighbourCount; i++) {
        message[position] = neighbours [i].id;
        position++;

        unsigned int networkCost = htonl(neighbours[i].cost);
        memcpy(message + position, & networkCost, 4); position +=4;
    }

    for (int i = 0; i < neighbourCount; i++) {
        int result = sendto(udpSocket, message, position, 0, (sockaddr*)& neighbours[i].address, sizeof(neighbours[i].address));

        if (result != position) {
            perror("sendto"); return false;
        }
    }

    cout << "Sent LSP from: " << myID << ", sequence: " << sequenceNumber << endl;

    return true;

}

int main(int argc, char* argv[]) {
    if (argc != 4) return 1;

    int udpPort = atoi(argv[3]);

    if (udpPort < 1 || udpPort > 65535) {
        cerr << "Invalid UDP Port\n"; return 1;
    }

    int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);

    if (udpSocket < 0) {
        perror("UDP socket"); return 1;
    }

    sockaddr_in myUDPAddress{};
    myUDPAddress.sin_family = AF_INET;
    myUDPAddress.sin_port = htons(udpPort);
    inet_pton(AF_INET, argv[2], &myUDPAddress.sin_addr);

    if (bind(udpSocket, (sockaddr*)&myUDPAddress, sizeof(myUDPAddress)) < 0)
        return 1;

    int tcpSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (tcpSocket < 0) {
        perror("TCP socket"); return 1;
    }

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

    unsigned int sequenceNumber = 0;

    int graph[26][26];
    bool known[26] = {};

    for (int i = 0; i < 26; i++) {
        for (int j = 0; j < 26; j++) {
            graph[i][j] = (i == j) ? 0 : -1;
        }
    }

    while (handleLinkStateMessage(tcpSocket, myID, neighbours, neighbourCount, graph, known)) {
        if (!sendOwnLSP(udpSocket, myID, neighbours, neighbourCount, sequenceNumber)) break;
    }

    close(tcpSocket);
    close(udpSocket);
    return 0;
}
