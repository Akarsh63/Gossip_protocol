#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <sstream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <fcntl.h>
#include <mutex>
#include <arpa/inet.h>
#include <ctime>
#include <cstdlib>
#include <condition_variable>
#include <queue>
#include <iomanip>
#include <openssl/sha.h>
using namespace std;

condition_variable cv;
queue<int> jobQueue;
vector<string> messagelist;
vector<string> union_peer_list;
vector<string> connseedadr;
mutex mtx;
string MY_IP = "0.0.0.0";
int port;

int socketserverpeer;

class Peer
{
private:
    int i;
    string address;

public:
    Peer(const string &addr) : address(addr), i(0) {}

    string getAddress() const
    {
        return address;
    }

    void setAddress(const string &addr)
    {
        address = addr;
    }

    int getI() const
    {
        return i;
    }

    void setI(int value)
    {
        i = value;
    }
};

vector<Peer> connected_peers;
vector<int> jobs = {1, 2, 3};
void write_output_to_file(const string &output)
{
    ofstream file;
    file.open("outputfile_peer.txt", ios_base::app);
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

vector<string> readseedaddr()
{
    vector<string> seed_adr;
    ifstream file("config.txt");
    if (!file)
    {
        cerr << "Error: file could not be opened" << endl;
        exit(1);
    }

    string line;
    while (getline(file, line))
    {
        seed_adr.push_back(line);
    }

    return seed_adr;
}

vector<int> generateRandom(int n, int k)
{
    vector<int> result;
    vector<bool> used(n, false);
    srand(time(NULL));
    while (result.size() < k)
    {
        int random = rand() % n;
        if (!used[random])
        {
            result.push_back(random);
            used[random] = true;
        }
    }

    sort(result.begin(), result.end());
    return result;
}

vector<string> splitString(const string &s, char delimiter)
{
    vector<string> tokens;
    stringstream ss(s);
    string token;
    while (getline(ss, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

void connecttopeers(vector<string> peerscon)
{
    for (auto &i : peerscon)
    {
        int socketpeer = socket(AF_INET, SOCK_STREAM, 0);
        if (socketpeer < 0)
        { // return -1 for an error
            std::cerr << "Peer Creation Error" << endl;
            return;
        }
        int col = i.find(":");
        sockaddr_in servadr;
        servadr.sin_family = AF_INET;
        servadr.sin_port = htons(stoi(i.substr(col + 1)));
        servadr.sin_addr.s_addr = inet_addr(i.substr(0, col).c_str());
        cout << "Connecting to " << i.substr(0, col) << ":" << i.substr(col + 1) << endl;
        int addrlen = sizeof(servadr);
        connect(socketpeer, (struct sockaddr *)&servadr, (socklen_t)addrlen);
        connected_peers.push_back(Peer(i));
        std::string message = "New Connect Request From:" + MY_IP + ":" + to_string(port);
        ssize_t bytes_sent = send(socketpeer, message.c_str(), message.length(), 0);
        char buffer[1024];
        ssize_t bytes_received = recv(socketpeer, buffer, 1024, 0);
        if (bytes_received > 0)
        {
            string message(buffer, bytes_received);
            cout << message << endl;
        }
        close(socketpeer);
    }
}

void create_socket()
{
    socketserverpeer = socket(AF_INET, SOCK_STREAM, 0);
    if (socketserverpeer < 0)
    { // return -1 for an error
        std::cerr << "Socket Creation Error" << endl;
        return;
    }
}

string sha256(const string &message)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, message.c_str(), message.length());
    SHA256_Final(hash, &sha256);
    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}

void forward_gossip_message(string &message, string &senderAddress, int type)

{
    string hashedmessage = sha256(message);
    if (find(messagelist.begin(), messagelist.end(), hashedmessage) != messagelist.end())
    {
        // continue;
        return;
    }
    else
    {
        if (type == 1)
        {
            write_output_to_file(message);
        }
        messagelist.push_back(hashedmessage);
    }
    vector<Peer> recipientAddresses;
    for (auto &peer : connected_peers)
    {
        if (peer.getAddress() != senderAddress)
        {
            recipientAddresses.push_back(peer);
        }
    }
    for (auto &peer : recipientAddresses)
    {
        try
        {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0)
            {
                cerr << "Socket creation error" << endl;
                return;
            }
            vector<string> peer_addr = splitString(peer.getAddress(), ':');
            sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_port = htons(stoi(peer_addr[1]));
            inet_pton(AF_INET, peer_addr[0].c_str(), &address.sin_addr);

            if (connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
            {
                throw runtime_error("Connection failed");
            }
            send(sock, message.c_str(), message.length(), 0);

            close(sock);
        }
        catch (...)
        {
            cout << "Peer Down" << peer.getAddress() << endl;
        }
    }
}

void handlepeer(int conn, struct sockaddr_in addr)
{
    char buffer[4096];
    while (true)
    {
        ssize_t bytes_received = recv(conn, buffer, sizeof(buffer), 0);
        if (bytes_received > 0)
        {
            string message(buffer, bytes_received);
            cout << message << endl;
            if (message.find("New Connect Request From:") == 0)
            {
                if (connected_peers.size() < 4)
                {
                    const char *output = "New peer connection accepted";
                    send(conn, output, strlen(output), 0);
                    int col = message.find(":");
                    connected_peers.push_back(Peer(message.substr(col + 1)));
                }
            }
            else if (message.find("Liveness Request") == 0)
            {
                vector<string> recvdata = splitString(message, ':');
                string liveness_reply = "Liveness Reply:" + recvdata[1] + ":" + recvdata[2] + ":" + MY_IP;
                send(conn, liveness_reply.c_str(), liveness_reply.length(), 0);
            }
            else
            {
                string senderAddress = inet_ntoa(addr.sin_addr);
                forward_gossip_message(message, senderAddress, 1);
            }
        }
        else
        {
            break;
        }
    }
}

void begin()
{
    struct sockaddr_in servadr;
    servadr.sin_family = AF_INET;
    servadr.sin_port = htons(port);
    servadr.sin_addr.s_addr = inet_addr(MY_IP.c_str());
    int addrlen = sizeof(servadr);
    bind(socketserverpeer, (struct sockaddr *)&servadr, (socklen_t)addrlen);
    listen(socketserverpeer, 5);
    cout << "Peer is Listening on " << MY_IP << ":" << to_string(port) << endl;
    while (1 > 0)
    {
        int peersock = accept(socketserverpeer, (struct sockaddr *)&servadr, (socklen_t *)&addrlen);
        if (peersock < 0)
        {
            perror("ERROR on accept");
            return;
        }
        int flags = fcntl(peersock, F_GETFL, 0);
        fcntl(peersock, F_SETFL | O_NONBLOCK);
        thread peer_thread(handlepeer, peersock, servadr);
        peer_thread.detach();
    }
    close(socketserverpeer);
}

void report_dead(string addr)
{
    vector<string> deadnodeadr = splitString(addr, ':');
    string deadmessage = "Dead Node:" + deadnodeadr[0] + ":" + deadnodeadr[1] + ":" + to_string(time(0)) + ":" + MY_IP;
    write_output_to_file(deadmessage);
    for (auto &x : connseedadr)
    {
        try
        {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0)
            {
                cerr << "Socket creation error" << endl;
                return;
            }
            vector<string> seed_addr = splitString(x, ':');
            sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_port = htons(stoi(seed_addr[1]));
            address.sin_addr.s_addr = INADDR_ANY;

            if (connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
            {
                throw runtime_error("Connection failed");
            }
            send(sock, deadmessage.c_str(), deadmessage.length(), 0);
            close(sock);
        }
        catch (...)
        {
            cout << "Connection to seed error,seed is down!" << endl;
        }
    }
}

void liveness_testing()
{
    while (1 > 0)
    {
        string liveness_request = "Liveness Request:" + to_string(time(0)) + ":" + MY_IP;
        for (auto &peer : connected_peers)
        {
            try
            {
                int sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock < 0)
                {
                    cerr << "Socket creation error" << endl;
                    return;
                }
                vector<string> peer_addr = splitString(peer.getAddress(), ':');
                sockaddr_in address;
                address.sin_family = AF_INET;
                address.sin_port = htons(stoi(peer_addr[1]));
                inet_pton(AF_INET, peer_addr[0].c_str(), &address.sin_addr);

                if (connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
                {
                    throw runtime_error("Connection failed");
                }
                send(sock, liveness_request.c_str(), liveness_request.length(), 0);
                char buffer[1024];
                ssize_t bytes_received = recv(sock, buffer, 1024, 0);
                if (bytes_received > 0)
                {
                    string reply(buffer, bytes_received);
                    cout << reply << endl;
                }
                close(sock);
                peer.setI(0);
            }
            catch (...)
            {
                peer.setI(peer.getI() + 1);
                if (peer.getI() == 3)
                {
                    cout << "Peer is dead of address: " << peer.getAddress() << endl;
                    report_dead(peer.getAddress());
                    auto it = find_if(connected_peers.begin(), connected_peers.end(), [&](const Peer &p)
                                      { return p.getAddress() == peer.getAddress(); });

                    if (it != connected_peers.end())
                    {
                        connected_peers.erase(it);
                    }
                }
            }
        }
        sleep(13);
    }
}

void gossip()
{
    for (int i = 0; i < 10; i++)
    {
        string gossip_message = to_string(time(0)) + ":" + MY_IP + ":" + to_string(port) + ":" + "GOSSIP" + to_string(i + 1);
        string myadr = MY_IP + ":" + to_string(port);
        forward_gossip_message(gossip_message, myadr, 0);
        sleep(5);
    }
}

void work()
{
    while (true)
    {
        int x;
        {
            unique_lock<mutex> lck(mtx);
            cv.wait(lck, []
                    { return !jobQueue.empty(); });
            x = jobQueue.front();
            jobQueue.pop();
            lck.unlock();
        }
        if (x == 1)
        {
            create_socket();
            begin();
        }
        else if (x == 2)
        {
            liveness_testing();
        }
        else if (x == 3)
        {
            gossip();
        }
    }
}

int main()
{
    cout << "Enter Port : ";
    string input;
    cin >> input;
    port = stoi(input);
    vector<string> seed_adr = readseedaddr();
    int n = seed_adr.size();
    vector<int> randseedind = generateRandom(n, n / 2 + 1);
    for (int i = 0; i < randseedind.size(); i++)
    {
        connseedadr.push_back(seed_adr[randseedind[i]]);
    }
    for (int i = 0; i < connseedadr.size(); i++)
    {
        int socketpeer = socket(AF_INET, SOCK_STREAM, 0);
        if (socketpeer < 0)
        { // return -1 for an error
            std::cerr << "Peer Creation Error" << endl;
            return 1;
        }
        int col = connseedadr[i].find(":");
        sockaddr_in servadr;
        servadr.sin_family = AF_INET;
        servadr.sin_port = htons(stoi(connseedadr[i].substr(col + 1)));
        servadr.sin_addr.s_addr = INADDR_ANY;
        connect(socketpeer, (struct sockaddr *)&servadr, sizeof(servadr));
        const char *message = (MY_IP + ":" + to_string(port)).c_str();
        send(socketpeer, message, strlen(message), 0);
        char buffer[1024];
        ssize_t bytes_received = recv(socketpeer, buffer, 1024, 0);
        vector<string> peer_list;
        if (bytes_received > 0)
        {
            string message(buffer, bytes_received);
            peer_list = splitString(message, ',');
            for (auto &x : peer_list)
            {
                int col = x.find(":");
                string myadr = x.substr(0, col) + ":" + to_string(port);
                if (x == myadr)
                {
                    MY_IP = x.substr(0, col);
                }
                if (x != myadr && find(union_peer_list.begin(), union_peer_list.end(), x) == union_peer_list.end())
                {
                    union_peer_list.push_back(x);
                }
            }
        }
        close(socketpeer);
    }
    string s = "";
    for (auto &peer : union_peer_list)
    {
        cout << peer << endl;
        s += peer;
        s += ",";
    }
    s = "Received peer list : " + s;
    write_output_to_file(s);
    vector<string> connpeeradr;
    if (union_peer_list.size() > 4)
    {
        vector<int> randpeerind = generateRandom(union_peer_list.size(), 4);
        for (int i = 0; i < randpeerind.size(); i++)
        {
            connpeeradr.push_back(union_peer_list[randpeerind[i]]);
        }
    }
    else
    {
        for (int i = 0; i < union_peer_list.size(); i++)
        {
            connpeeradr.push_back(union_peer_list[i]);
        }
    }
    connecttopeers(connpeeradr);

    thread workers[3];
    for (int i = 0; i < 3; ++i)
    {
        workers[i] = thread(work);
    }

    for (auto &i : jobs)
    {
        unique_lock<mutex> lck(mtx);
        jobQueue.push(i);
        lck.unlock();
        cv.notify_one();
    }

    for (int i = 0; i < 3; ++i)
    {
        workers[i].join();
    }
    return 0;
}