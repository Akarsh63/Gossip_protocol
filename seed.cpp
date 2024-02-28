#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <fstream>
#include <algorithm>
#include <fcntl.h>
#include <mutex>
#include <arpa/inet.h>

using namespace std;

vector<string> peer_list;
mutex mtx;
int portNum;

void write_output_to_file(const string &output)
{
    ofstream file;
    file.open("outputfile_seed.txt", ios_base::app);
    if (file.is_open())
    {
        file << output << endl;
        file.close();
    }
    else
    {
        cerr << "Write Failed" << endl;
    }
    return;
}

void remove_dead_node(string &message)
{
    cout << message << endl;
    write_output_to_file(message);
    size_t first_colon = message.find(':');
    size_t second_colon = message.find(':', first_colon + 1);
    size_t third_colon = message.find(':', second_colon + 1);
    string deadnode = message.substr(first_colon + 1, third_colon - first_colon - 1);
    mtx.lock();
    auto iterator = find(peer_list.begin(), peer_list.end(), deadnode);
    if (iterator != peer_list.end())
    {
        peer_list.erase(iterator);
    }
    mtx.unlock();
    return;
}

string list_to_string(const vector<string> &peer_list)
{
    string PeerList = "";
    for (int i = 0; i < peer_list.size(); i++)
    {
        if (i != peer_list.size() - 1)
        {
            PeerList += peer_list[i] + ",";
        }
        else
        {
            PeerList += peer_list[i];
        }
    }
    return PeerList;
}

void handlepeer(int conn, struct sockaddr_in addr)
{
    char buffer[1024];
    while (1 > 0)
    {
        ssize_t bytes_received = recv(conn, buffer, 1024, 0);
        if (bytes_received > 0)
        {
            string message(buffer, bytes_received);
            if (message.find("Dead Node") == 0)
            {
                remove_dead_node(message);
            }
            else
            {
                size_t colon = message.find(":");

                string peer = string(inet_ntoa(addr.sin_addr)) + ":" + message.substr(colon + 1);
                mtx.lock();
                peer_list.push_back(peer);
                mtx.unlock();
                string output = "New peer connection is received from address :" + peer + " to: " + to_string(portNum);
                cout << output << endl;
                write_output_to_file(output);
                string PeerList = list_to_string(peer_list);
                send(conn, PeerList.c_str(), PeerList.size(), 0);
            }
        }
        else
        {
            break;
        }
    }
    close(conn);
}

int main()
{
    int socketserver = socket(AF_INET, SOCK_STREAM, 0);
    if (socketserver < 0)
    { // return -1 for an error
        std::cerr << "Socket Creation Error" << endl;
        return 1;
    }
    cout << "Enter port number of seed : ";
    cin >> portNum;

    struct sockaddr_in servadr;
    servadr.sin_family = AF_INET;
    servadr.sin_port = htons(portNum);
    servadr.sin_addr.s_addr = INADDR_ANY;
    int addrlen = sizeof(servadr);
    bind(socketserver, (struct sockaddr *)&servadr, (socklen_t)addrlen);

    listen(socketserver, 5);
    cout << "Seed is Listening" << endl;
    while (1 > 0)
    {
        int peersock = accept(socketserver, (struct sockaddr *)&servadr, (socklen_t *)&addrlen);

        if (peersock < 0)
        {
            perror("ERROR on accept");
            return 1;
        }
        int flags = fcntl(peersock, F_GETFL, 0);
        fcntl(peersock, F_SETFL, O_NONBLOCK);
        thread peer_thread(handlepeer, peersock, servadr);
        peer_thread.detach();
    }
    close(socketserver);
}