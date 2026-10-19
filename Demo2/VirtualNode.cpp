#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <climits>
#include <sys/select.h>
#include <ctime>
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

        if (cost > INT_MAX) return false;

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

            newNeighbours[newCount].cost = (int)cost;
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

void handleReceivedLSP(int udpSocket, char myID, const Neighbour neighbours[], int neighbourCount, int graph[26][26], bool known[26], unsigned int lastSequence[26]) {
    char message[6 + 5 * 26];
    sockaddr_in sender{};
    socklen_t senderLength = sizeof(sender);

    int length = recvfrom(udpSocket, message, sizeof(message), 0, (sockaddr*)&sender, &senderLength);

    if (length < 6) return;

    char origin = message[0];
    if (origin < 'A' || origin > 'Z' || origin == myID) return;

    unsigned int networkSequence;
    memcpy(&networkSequence, message + 1, 4);
    unsigned int sequence = ntohl(networkSequence);

    int count = (unsigned char)message[5];

    if (sequence == 0 || count > 25 || length != 6 + count * 5) return;

    int index = nodeIndex(origin);
    if (sequence <= lastSequence[index]) return;

    Neighbour links[26];
    bool seen[26] = {};
    int position = 6;

    for (int i = 0; i < count; i++) {
        char id = message[position++];
        unsigned int networkCost;

        if (id < 'A' || id > 'Z' || id == origin) return;

        int neighbourIndex = nodeIndex(id);
        if (seen[neighbourIndex]) return;
        seen[neighbourIndex] = true;

        memcpy(&networkCost, message + position, 4);
        position += 4;

        unsigned int cost = ntohl(networkCost);
        if (cost == 0 || cost > INT_MAX) return;

        links[i].id = id; links[i].cost = (int)cost;
    }

    updateGraph(origin, links, count, graph, known);
    lastSequence[index] = sequence;

    for (int i = 0; i < neighbourCount; i++) {
        if (neighbours[i].address.sin_addr.s_addr == sender.sin_addr.s_addr && neighbours[i].address.sin_port == sender.sin_port) continue;
        int sent = sendto(udpSocket, message, length, 0, (sockaddr*)&neighbours[i].address, sizeof(neighbours[i].address));

        if (sent != length) perror("Forward LSP");
    }

    cout << "Received new LSP from: " << origin << ", sequence: " << sequence << endl;
    printGraph(graph, known);

}

int main(int argc, char* argv[]) {
    if (argc != 4) return 1;

    int udpPort = atoi(argv[3]);

    if (udpPort < 1 || udpPort > 65535) {
        cerr << "Invalid UDP Port\n"; return 1;
    }

    in_addr onIP{}, myIP{};

    if (inet_pton(AF_INET, argv[1], &onIP) != 1 || inet_pton(AF_INET, argv[2], &myIP) != 1) {
        cerr << "Invalid IPv4 address\n"; return 1;
    }

    unsigned int onHost = ntohl(onIP.s_addr);
    unsigned int myHost = ntohl(myIP.s_addr);

    if (onHost == 0 || myHost == 0 || (onHost >> 24) == 127 || (myHost >> 24) == 127) {
        cerr << "loopback/unspecified IPv4 address provided\n"; return 1;
    }

    int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);

    if (udpSocket < 0) {
        perror("UDP socket"); return 1;
    }

    sockaddr_in myUDPAddress{};
    myUDPAddress.sin_family = AF_INET;
    myUDPAddress.sin_port = htons(udpPort);
    myUDPAddress.sin_addr = myIP;

    if (bind(udpSocket, (sockaddr*)&myUDPAddress, sizeof(myUDPAddress)) < 0)
        return 1;

    int tcpSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (tcpSocket < 0) {
        perror("TCP socket"); return 1;
    }

    sockaddr_in oracleAddress{};
    oracleAddress.sin_family = AF_INET;
    oracleAddress.sin_port = htons(5000);
    oracleAddress.sin_addr = onIP;

    if (connect(tcpSocket, (sockaddr*)&oracleAddress, sizeof(oracleAddress)) < 0)
        return 1;

    char message[6];
    memcpy(message, &myIP.s_addr, 4);

    unsigned short networkPort = htons(udpPort);
    memcpy(message + 4, &networkPort, 2);

    if (!sendAll(tcpSocket, message, 6)) return 1;

    cout << "Connected to ON. Waiting for LINK-STATE...\n";

    char myID = '?';
    Neighbour neighbours[26];
    int neighbourCount = 0;

    unsigned int sequenceNumber = 0;
    unsigned int lastSequence[26] = {};

    int graph[26][26];
    bool known[26] = {};

    for (int i = 0; i < 26; i++) {
        for (int j = 0; j < 26; j++) {
            graph[i][j] = (i == j) ? 0 : -1;
        }
    }

    time_t lastPeriodic = time(nullptr);

    while (true) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(tcpSocket, &readSet);

        if (myID != '?')
        FD_SET(udpSocket, &readSet);

        int maxSocket = (tcpSocket > udpSocket) ? tcpSocket : udpSocket;

        timeval timeout{};
        timeval* timeoutPointer = nullptr;

        if (myID != '?') {
            int elapsed = (int)(time (nullptr) - lastPeriodic);
            int remaining = 15 - elapsed;
            if (remaining < 0) remaining = 0;

            timeout.tv_sec = remaining;
            timeoutPointer = &timeout;
        }

        int ready = select(maxSocket + 1, &readSet, nullptr, nullptr, timeoutPointer);

        if (ready < 0) {
            perror("select");
            break;
        }

        if (ready > 0 && FD_ISSET(tcpSocket, &readSet)) {
            if (!handleLinkStateMessage(tcpSocket, myID, neighbours, neighbourCount, graph, known))
            break;
            if (!sendOwnLSP(udpSocket, myID, neighbours, neighbourCount, sequenceNumber)) break;
            lastSequence[nodeIndex(myID)] = sequenceNumber;
            lastPeriodic = time(nullptr);
        }

        if (ready > 0 && FD_ISSET(udpSocket, &readSet)) {
            handleReceivedLSP(udpSocket, myID, neighbours, neighbourCount, graph, known, lastSequence);
        }

        if (myID != '?' && time(nullptr) - lastPeriodic >= 15) {
            if (!sendOwnLSP(udpSocket, myID, neighbours, neighbourCount, sequenceNumber)) break;

            lastSequence[nodeIndex(myID)] = sequenceNumber;
            printGraph(graph, known);
            lastPeriodic = time(nullptr);
        }

    }

    close(tcpSocket);
    close(udpSocket);
    return 0;
}
