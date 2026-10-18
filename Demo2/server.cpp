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

int main(int argc, char* argv[]) {
    if(argc!=2){
        cout<< "Usage is ./ON <configfile>\n";
        return 1;
    }

    // Parsing the config file

    int N;
    vector<vector<int>> costs;
    int temp = 1;

    ifstream fin(argv[1]);

    string line;
    while(getline(fin,line)){
        if(line.empty()) continue; 
        else if(line[0] == '#') continue; // skipping the comment lines
        else if(line[0] == 'N'){ // N = <value>
            char ch;
            istringstream iss(line);
            iss >> ch >> ch >> N;

            costs.assign(N,vector<int>(N,-1));
            for(int i=0;i<N;i++){
                costs[i][i] = 0;
            }
        }
        else{
            istringstream iss(line);
            int x;
            int row = temp -1;
            int col = temp;
            while(iss >> x){
                costs[row][col]=x;
                costs[col][row]=x;
                col++;
            }
            temp++;
        }

        int serverSock = socket(AF_INET, SOCK_STREAM, 0);
        if(serverSock < 0){
            perror("socket creation error");
            return 1;
        }


        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(5000);

        inet_pton(AF_INET, <IPADDR>, &serverAddr.sin_addr); ///////////

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

        vector<int> clientSocks;
        int i=0;
        while(i<N){
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);

            int clientSock = accept(serverSock,(struct sockaddr*)&clientAddr,&clientLen);
            if(clientSock < 0){
                perror("Accept error");
                
            }

            i++;

        }

    }
    return 0;
}