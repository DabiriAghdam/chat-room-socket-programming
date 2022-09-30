#include <exception>
#include <iostream>
#include <sstream>
#include <vector>

#include <time.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "message.h"

#define DEFAULT_SRV_HOST "127.0.0.1"
#define DEFAULT_SRV_PORT 9000
#define MAX_PAYLOAD_LENGTH 10000
#define WAIT_TIME 1

using namespace std;

void client_write(int fd, const uint8_t* buffer, size_t len) 
{
    auto n = 0;
    while (n < len) 
    {
        auto ret = write(fd, buffer + n, len - n);
        if (ret < 0)
            throw runtime_error("failed to write");
        if (ret == 0)
            throw runtime_error("socket closed");

        n += ret;
    }
}

void client_read(int fd, uint8_t* buffer, size_t len) {
    auto n = 0;
    while (n < len) 
    {
        auto ret = read(fd, buffer + n, len - n);
        if (ret < 0)
            throw runtime_error("failed to read");
        if (ret == 0)
            throw runtime_error("socket closed");

        n += ret;
    }
}

void send_username(int fd, string username)
{
    char buffer[MAX_PAYLOAD_LENGTH + sizeof(Header) + 1];
    Header* hdr_ptr;
    hdr_ptr = (Header*)buffer; 

    hdr_ptr->message_type = CONNECT;
    hdr_ptr->length = username.length() + 2;   
    client_write(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));
    client_write(fd, (uint8_t*)username.c_str(), username.length());
    
    while(hdr_ptr->message_type != CONNACK)
        client_read(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));
}

int connect_to_host(string host, uint16_t port, string name)
{
    auto srv_addr = sockaddr_in{sin_family: AF_INET, sin_port: htons(port)};
    auto err = inet_pton(AF_INET, host.c_str(), &srv_addr.sin_addr);
    if (err < 0)
    {
        cerr << "failed to complete address " << err << endl;
        return EXIT_FAILURE;
    }

    auto fd = socket(AF_INET, SOCK_STREAM, 0);

    while(1)
    {
        if (fd < 0)
        {
            cerr << "failed to create socket " << fd << endl;
            goto wait;
        }
        err = connect(fd, (sockaddr*)&srv_addr, sizeof(srv_addr));
        if (err < 0)
        {
            cerr << "failed to connect: " << err << endl;
            goto exit;
        }

        send_username(fd, name);
        clog << "connected to " << host << ':' << port << " (your username is " << name << ")" << endl;
        cout << ">> ";
        cout.flush();
        break;

        exit:
            close(fd);

        wait:
            sleep(1); 
    }
    return fd;
}

void receive_message(int fd)
{
    char buffer[MAX_PAYLOAD_LENGTH + sizeof(Header) + 1];
    Header* hdr_ptr;
    hdr_ptr = (Header*)buffer; 

    hdr_ptr->message_type = RECEIVE;
    hdr_ptr->length = sizeof(Header);   
    client_write(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));
    while(hdr_ptr->message_type != RECEIVEREPLY)
        client_read(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));

    if (hdr_ptr->length > 4)
    {
        int payload_length = hdr_ptr->length - sizeof(Header) - 2;
        if (payload_length < 0)
            throw runtime_error("negative payload length");
        if (payload_length < 1)
            throw runtime_error("wrong message length");

        uint8_t payload[MAX_PAYLOAD_LENGTH + 1];
        client_read(fd, payload, 2);
        int uid = ntohs(*(uint16_t*)payload);

        client_read(fd, payload, payload_length);
        payload[payload_length] = 0;
        string msg = string((char*)payload);

        hdr_ptr->message_type = INFO;
        hdr_ptr->length = 2 + sizeof(Header);   
        client_write(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));
        uint16_t buf = htons(uid);
        client_write(fd, (uint8_t*)&buf, sizeof(buf));
        while(hdr_ptr->message_type != INFOREPLY)
            client_read(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));
        
        payload_length = hdr_ptr->length - sizeof(Header);
        if (payload_length < 0)
            throw runtime_error("negative payload length");
        if (payload_length < 1)
            throw runtime_error("wrong message length");

        client_read(fd, payload, payload_length);
        payload[payload_length] = 0;
        string user_name = string((char*)payload);

        cout << "\b\b\b<< " << user_name << ":" << msg << endl;
        cout << ">> ";
        cout.flush();
    }
}

vector<int> get_list(int fd, bool verbose = false)
{
    char buffer[MAX_PAYLOAD_LENGTH + sizeof(Header) + 1];
    Header* hdr_ptr;
    hdr_ptr = (Header*)buffer; 

    hdr_ptr->message_type = LIST;
    hdr_ptr->length = sizeof(Header);   
    client_write(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));
    while(hdr_ptr->message_type != LISTREPLY)
        client_read(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));

    int payload_length = hdr_ptr->length - sizeof(Header);
    if (payload_length < 0)
        throw runtime_error("negative payload length");
    if (payload_length < 1)
        throw runtime_error("wrong message length");
    uint8_t payload[MAX_PAYLOAD_LENGTH + 1];
    vector<int> ids;
    for (int i = 0; i < payload_length / 2; i++)
    {
        client_read(fd, payload, 2);
        ids.push_back(ntohs(*(uint16_t*)payload));
    }

    for (int i = 0; i < ids.size(); i++)
    {
        hdr_ptr->message_type = INFO;
        hdr_ptr->length = 2 + sizeof(Header);   
        client_write(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));
        uint16_t buf = htons(ids[i]);
        client_write(fd, (uint8_t*)&buf, sizeof(buf));

        while(hdr_ptr->message_type != INFOREPLY)
            client_read(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));

        payload_length = hdr_ptr->length - sizeof(Header);
        if (payload_length < 0)
            throw runtime_error("negative payload length");
        if (payload_length < 1)
            throw runtime_error("wrong message length");
        uint8_t payload[MAX_PAYLOAD_LENGTH + 1];
        client_read(fd, payload, payload_length);
        payload[payload_length] = 0;
        if (verbose)
        {
            string user_name = string((char*)payload);
            cout << "- " << user_name << endl;
        }
    }

    return ids;
}

void send_message(int fd, vector<int> ids, string username, string msg)
{
    char buffer[MAX_PAYLOAD_LENGTH + sizeof(Header) + 1];
    Header* hdr_ptr;
    hdr_ptr = (Header*)buffer; 

    int uid = -1;
    for (int i = 0; i < ids.size(); i++)
    {
        hdr_ptr->message_type = INFO;
        hdr_ptr->length = 2 + sizeof(Header);   
        client_write(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));
        uint16_t buf = htons(ids[i]);
        client_write(fd, (uint8_t*)&buf, sizeof(buf));

        while(hdr_ptr->message_type != INFOREPLY)
            client_read(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));

        int payload_length = hdr_ptr->length - sizeof(Header);
        if (payload_length < 0)
            throw runtime_error("negative payload length");
        if (payload_length < 1)
            throw runtime_error("wrong message length");
        uint8_t payload[MAX_PAYLOAD_LENGTH + 1]; 
        client_read(fd, payload, payload_length);
        payload[payload_length] = 0;
        if (string((char*)payload) == username)
        {
            uid = ids[i];
            break;
        }
    }

    if (uid == -1)
        cerr << "user not found" << endl;

    else
    {
        hdr_ptr->message_type = SEND;
        hdr_ptr->length = 2 + sizeof(Header) + msg.length();   
        client_write(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));
        uint16_t buf = htons(uid); 
        client_write(fd, (uint8_t*)&buf, sizeof(buf));
        client_write(fd, (uint8_t*)msg.c_str(), msg.length());
        while(hdr_ptr->message_type != SENDREPLY)
            client_read(fd, (uint8_t*)hdr_ptr, sizeof(*hdr_ptr));

        uint8_t payload; 
        client_read(fd, &payload, 1);
        if (payload == 0xff)
            clog << "message sent" << endl;
        else
            cerr << "sending failed" << endl;
    }
}

int handle_commands(int fd)
{
    string line;
    if (!getline(cin, line) || line == "exit")
    {
        clog << "exiting" << endl;
        return 0;
    }

    else if (line == "list")
        get_list(fd, true);

    else if (line.find("send") != string::npos)
    {
        string buf;                 
        stringstream ss(line);      
        vector<string> tokens;
        while (ss >> buf)
            tokens.push_back(buf);
        
        if (tokens[0] != "send" || tokens.size() < 3)
            cerr << "wrong input format" << endl;

        else
        {
            string username = tokens[1];
            string msg = "";
            for (int i = 2; i < tokens.size(); i++)
                msg += ' ' + tokens[i];
            auto ids = get_list(fd);
            send_message(fd, ids, username, msg);
        }
    }

    else
        cerr << "wrong command" << endl;
        
    cout << ">> ";
    cout.flush();

    return 1;
}

string gen_random(const int len) 
{
    static const char alphanum[] =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz";
    string tmp;
    tmp.reserve(len);
    for (int i = 0; i < len; ++i) 
        tmp += alphanum[rand() % (sizeof(alphanum) - 1)];
    
    return tmp;
}

int main(int argc, char** argv)
{
    srand((unsigned)time(NULL) * getpid());   

    string name = "user_" + gen_random(3);
    string host = DEFAULT_SRV_HOST;
    int port = DEFAULT_SRV_PORT;
    /* handling input arguments*/
    try 
    {
        if (argc > 1)
        {
            auto temp = string((char*)argv[1]);
            auto found = temp.find(":");
            if (found == string::npos)
            {
                cerr << "invalid arguments" << endl;
                return EXIT_FAILURE;
            }
            host = temp.substr(0, found);
            port = stoi(temp.substr(found + 1));
            if (port < 0 || port > 65536)
            {
                cerr << "invalid arguments" << endl;
                return EXIT_FAILURE;
            }
        }
        if (argc > 2)
            name = string((char*)argv[2]);
    }
    catch (exception& e) 
    {
        cerr << "error: " << e.what() << endl;
        cerr << "invalid arguments" << endl;
        return EXIT_FAILURE;
    }

    auto fd = connect_to_host(host, port, name);
    if (fd == EXIT_FAILURE)
        return EXIT_FAILURE;
    
    /* main loop */
    while(1)
    {
        try
        {
            fd_set read_fds;
            FD_ZERO (&read_fds);
            FD_SET(STDIN_FILENO, &read_fds);
            auto tv = timeval{WAIT_TIME, 0};
            auto ret = select(2, &read_fds, NULL, NULL, &tv);

            if (ret < 0)
                throw runtime_error("select error");

            /* Asks server for new messages every 1 minute */
            else if (ret == 0)
                receive_message(fd);

            /* Handling user commands */
            if (FD_ISSET(STDIN_FILENO, &read_fds))
            {
                auto result = handle_commands(fd);        
                if (!result) 
                    break;
            }
        }
        catch (exception& e)
        {
            cerr << "error: " << e.what() << endl;
            cerr << "if the above error occurs repeatedly, consider restarting the app or your system" << endl;
            cout << ">> ";
            cout.flush();
        }
    }

    return EXIT_SUCCESS;
}