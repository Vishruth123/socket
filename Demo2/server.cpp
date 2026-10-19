#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

using namespace std;

struct VirtualNode{
    char nodeID;
    int clientSock;
    in_addr ipAddr; //////////////////////////
    int udpPort;
};

bool recmsg(int sock, void* buffer, int length){
    char*p = (char*)buffer;
    int received = 0;

    while(received < length) {
        int n = recv(sock, p+received , length - received, 0);
        if(n<=0) return false;
        received += n;
    }
    return true;
}

bool sendcompletemsg(int sock, const void* buffer, int length) {
    const char* p = (char*)buffer;
    int sent = 0;

    while(sent<length){
        int n = send(sock, p+sent, length - sent, 0);
        if(n<=0) return false;
        sent += n;
    }
    return true;
}

void msgtuple(vector<unsigned char>& body,const VirtualNode& node,int cost){
    body.push_back(node.nodeID);
    unsigned char* ip = (unsigned char*)&node.ipAddr.s_addr;
    for(int i=0;i<4;i++){
        body.push_back(ip[i]);
    }
    unsigned short port = node.udpPort;

    body.push_back((port >> 8) & 0xFF);
    body.push_back(port & 0xFF);

    uint32_t tcost = cost;
    body.push_back((tcost >> 24) & 0xFF);
    body.push_back((tcost >> 16) & 0xFF);
    body.push_back((tcost >> 8) & 0xFF);
    body.push_back(tcost & 0xFF);
}

bool readconfig(const string& filename, vector<vector<int>>& result){
    ifstream fin(filename);
    if(!fin.is_open()){
        perror("config file didnt open");
        return 1;
    }

    vector<vector<int>> rows;
    string line;

    while(getline(fin,line)){
        size_t first = line.find_first_not_of(" \t\r\n");
        if(first == string::npos) continue;
        if(line[first] == '#') continue;
        istringstream iss(line);
        vector<int> row;
        int x;
        while(iss >> x){
            row.push_back(x);
        }
        if(row.empty()){
            perror("invalid row format in config file");
            return false;
        }
        rows.push_back(row);
    }
    fin.close();
    int N = rows.size() + 1;
    for(int i=0;i<N-1;i++){
        if(rows[i].size()!= N-i-1){
            perror("wrong number of values in this row");
            return false;
        }
        for(int cost:rows[i]){
            if(cost!=-1 && cost<=0){
                perror("invalid cost value");
                return false;
            }
        }
    }
    vector<vector<int>> costs(N,vector<int>(N,-1));
    for(int i = 0;i<N;i++){
        costs[i][i] = 0;
    }
    for(int i=0;i<N-1;i++){
        for(int j=i+1;j<N;j++){
            int cost = rows[i][j-i-1];
            costs[i][j] = cost;
            costs[j][i] = cost;
        }
    }
    result = costs;
    return true;
}

bool sendLinkstate(int nodeID, const vector<VirtualNode>& nodes,const vector<vector<int>>& costs){
    int N = costs.size();
    vector<unsigned char> body;
    unsigned char count = 0;
    for(int k=0;k<N;k++){
        if(k==nodeID || costs[nodeID][k] != -1) count++;
    }
    body.push_back(count);
    for(int k=0;k<N;k++){
        if(k==nodeID){
            msgtuple(body,nodes[k],0);
        }
        else if(costs[nodeID][k] != -1){
            msgtuple(body,nodes[k],costs[nodeID][k]);
        }
    }
    return sendcompletemsg(nodes[nodeID].clientSock,body.data(),body.size());
}

int main(int argc, char* argv[]) {
    if(argc!=2){
        cout<< "Usage is ./ON <configfile>\n";
        return 1;
    }

    // Parsing the config file
    // Need to add more validating the config file rules
    string configfile = argv[1];
    vector<vector<int>> costs;
    if(!readconfig(configfile,costs)){
        return 1;
    }
    int N = costs.size();
    

    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSock < 0){
        perror("socket creation error");
        return 1;
    }


    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(5000);

    if(inet_pton(AF_INET, "172.18.128.3", &serverAddr.sin_addr) != 1){
        perror("inet_pton error");
        close(serverSock);
        return 1;
    }; ///////////

    if(bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0){
        perror("bind");
        close(serverSock);
        return 1;
    };

    if(listen(serverSock, 27) < 0){
        perror("listen function error");
        close(serverSock);
        return 1;
    };

    vector<VirtualNode> nodes;
    int i=0;
    while(i<N){
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        int clientSock = accept(serverSock,(struct sockaddr*)&clientAddr,&clientLen);
        if(clientSock < 0){
            perror("Accept error");
            // continue;
            close(serverSock);
            return 1;
        }

        unsigned char body[6];
        if(!recmsg(clientSock,body,6)){
            cerr << "failed to rec CONNECT body";
            close(clientSock);
            continue;
        }
        VirtualNode node{};
        node.nodeID = 'A' + i;
        node.clientSock = clientSock;
        memcpy(&node.ipAddr.s_addr,body,4);
        uint16_t portno;
        memcpy(&portno,body+4,2);
        node.udpPort = ntohs(portno);
        nodes.push_back(node);

        i++;
    }
    for(int j=0;j<N;j++){
        if(!sendLinkstate(j,nodes,costs)){
            cerr << "Failure in sending initial link-state to nodes";
            return 1;
        }
    }

    while(true){
        sleep(30);

        vector<vector<int>> newcosts;
        if(!readconfig(configfile,newcosts)){
            cerr << "config file re parsing failed";
            continue;
        }
        if(newcosts == costs) continue;
        costs = newcosts;
        for(int j=0;j<N;j++){
            if(!sendLinkstate(j,nodes,costs)){
                perror("failure in update of node");
            }
        }
    }
    
    return 0;
}