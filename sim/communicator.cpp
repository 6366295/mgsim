#include "communicator.h"
#include "messageprocessor.h"

#include <signal.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <linux/sockios.h>
#include <sys/ioctl.h>

#include <cstdio>
#include <cerrno>

#include <iostream>

#include "rapidjson/writer.h"

#define pthread(Function, ...) do { if (pthread_ ## Function(__VA_ARGS__)) perror("pthread_" #Function); } while(0)

#define PORTNUM 2300

using namespace std;


// Block signals for server thread
void* runserver(void* arg)
{
    sigset_t sigset;
    
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGTERM);
    
    pthread_sigmask(SIG_BLOCK, &sigset, 0);

    Communicator* comm = (Communicator*) arg;
    comm->server();
    
    return 0;
}

// Block signals for data/status sender thread
void* runsender(void* arg)
{
    sigset_t sigset;
    
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGTERM);
    
    pthread_sigmask(SIG_BLOCK, &sigset, 0);

    Communicator* comm = (Communicator*) arg;
    comm->sender();
    
    return 0;
}

Communicator::Communicator(Simulator::MGSystem& sys, CommandLineReader* clr, bool enabled, bool quiet)
    : m_sys(sys),
      m_clr(clr),
      m_msgprocessor(0),
      m_serverthread(),
      m_senderthread(),
      m_quiet(quiet),
      m_enabled(true)
{
    // Don't enable communicator threads
    if(!enabled)
    {
        if(!m_quiet)
            clog << "! server disabled" << endl;
        
        m_enabled = false;
        
        return;
    }
    
    // Enable communicator threads
    if(InitServer() == 1)
    {
        if(!m_quiet)
            clog << "! init server failed. server disabled" << endl;

        m_enabled = false;

        return;
    }
    
    if(!m_quiet)
        clog << "! server enabled" << endl;

    m_msgprocessor = new MessageProcessor(m_sys, m_clr);

    pthread(create, &m_serverthread, 0, runserver, this);
    pthread(create, &m_senderthread, 0, runsender, this);
}

Communicator::~Communicator()
{
    int i;

    if(m_enabled)
    {
        if(!m_quiet)
            clog << "! shutting down server..." << endl;
        
        // Get threads out of while loop
        m_enabled = false;
        write(pipe_fd[1], "shutdown pls", sizeof("shutdown pls"));
        
        pthread(join, m_serverthread, 0);
        pthread(join, m_senderthread, 0);
        
        // Close open client sockets
        for(i = 0 ; i < max_clients ; i++)
        {
            if(clients[i].initialized)
                close(clients[i].socket);
        }
        
        // Close server socket
        close(server_socket);

        close(pipe_fd[0]);
        close(pipe_fd[1]);
        
        if(!m_quiet)
            clog << "! server shutdown" << endl;
    }
}

int Communicator::InitServer()
{ 
    struct sockaddr_in server_addr_in;

    int portnum;
    

    server_socket = socket(AF_INET , SOCK_STREAM , 0);

    if(server_socket == 0)
    {
        if(!m_quiet)
            perror("! server_socket failed");

        return 1;
    }

    portnum = m_sys.GetConfig().getValueOrDefault<Simulator::PSize>("CommunicatorPort", PORTNUM);

    if(!m_quiet)
        clog << "! communicator will use port: "<< portnum << endl;
    
    server_addr_in.sin_family = AF_INET;
    server_addr_in.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr_in.sin_port = htons(portnum);
    
    if(bind(server_socket, (struct sockaddr *)&server_addr_in, sizeof(struct sockaddr_in)) < 0)
    {
        if(!m_quiet)
            perror("! server socket bind failed");

        return 1;
    }
    
    if(listen(server_socket, 1) < 0)
    {
        if(!m_quiet)
            perror("! listen failed");

        return 1;
    }

    return 0;
}

void Communicator::server()
{
    if(!m_quiet)
        clog << "! server thread started" << endl;
    
    struct sockaddr_in  client_addr_in;
    struct timeval      tv;

    socklen_t           socksize;
    fd_set              socket_fds;

    char                buffer[MAXRCVLEN + 1];   
    int                 sockd, max_sockd;
    int                 new_socket;
    int                 i;

    socksize = sizeof(struct sockaddr_in);

    // Timeout duration of select function
    tv = {0, 100};

    pipe(pipe_fd);

    while(m_enabled) 
    {
        FD_ZERO(&socket_fds);
        
        FD_SET(server_socket, &socket_fds);
        max_sockd = server_socket;
            
        for(i = 0 ; i < max_clients ; i++) 
        {
            sockd = clients[i].socket;
            
            if(sockd > 0)
                FD_SET(sockd , &socket_fds);
            
            if(sockd > max_sockd)
                max_sockd = sockd;
        }

        FD_SET(pipe_fd[0], &socket_fds);
        if(pipe_fd[0] > max_sockd)
                max_sockd = pipe_fd[0];
        
        // TODO: error check??
        // make blocking, add fd for shutdown; Done
        select(max_sockd + 1, &socket_fds, NULL, NULL, NULL);
        
        // New client
        if(FD_ISSET(server_socket, &socket_fds)) 
        { 
            // TODO to be removed
            //clog << "! new connection from ip,port: " << inet_ntoa(client_addr_in.sin_addr) << "," << ntohs(client_addr_in.sin_port) << endl;
              
            for(i = 0; i < max_clients; i++) 
            {
                if(clients[i].socket == 0)
                {
                    new_socket = accept(server_socket, (struct sockaddr *)&client_addr_in, &socksize);

                    clients[i].socket = new_socket;
                    
                    break;
                }
            }
        }
        
        // Message from client
        for(i = 0; i < max_clients; i++) 
        {
            sockd = clients[i].socket;
              
            if(FD_ISSET(sockd, &socket_fds)) 
            {
                // Client disconnects
                if((length = recv(sockd, buffer, MAXRCVLEN, 0)) == 0)
                {
                    getpeername(sockd , (struct sockaddr*)&client_addr_in , &socksize);
                    // TODO to be removed
                    //clog << "! " << inet_ntoa(client_addr_in.sin_addr) << "," << ntohs(client_addr_in.sin_port) << " disconnected" << endl;
                    
                    // Reset client for reuse
                    close(sockd);
                    clients[i] = client();
                }
                // Process message
                else
                {
                    size_t pos;
                    string readbuffer;

                    if(length != -1)
                        readbuffer = string(buffer, length);

                    if((pos = readbuffer.find("\n")) != (size_t) -1)
                    {
                        m_msgprocessor->process(&clients[i], clients[i].buffer + readbuffer.substr(0, pos));

                        readbuffer = readbuffer.substr(pos+1);

                        while((pos = readbuffer.find("\n")) != (size_t)-1)
                        {
                            m_msgprocessor->process(&clients[i], readbuffer.substr(0, pos));

                            readbuffer = readbuffer.substr(pos+1);
                        }

                        clients[i].buffer = readbuffer;
                    }
                    else
                    {
                        clients[i].buffer.append(readbuffer);
                    }
                }
            }
        }
    }
}

void Communicator::sender()
{
    if(!m_quiet)
        clog << "! data/status sender thread started" << endl;

    string              message;
    float               delay;
    int                 status, count, i, step;

    while(m_enabled)
    {
        status = m_sys.GetStatus();

        m_tsdelay = m_msgprocessor->m_tsdelay;

        delay = 0.0;

        delay += m_tsdelay.tv_sec;
        delay += (m_tsdelay.tv_nsec / 1000000000.);

        step = m_clr->step;

        for(i = 0; i < max_clients; i++)
        {
            // Only do stuff, when client is initialized
            if(clients[i].initialized)
            {
                // Dont't send data, when selection is changing
                if(clients[i].changed)
                    continue;

                // Works only for linux??
                // Send only when socket is empty
                ioctl(clients[i].socket, SIOCOUTQ, &count);
                if(count != 0)
                {
                    continue;
                }

                // Create message 
                StringBuffer s;
                Writer<StringBuffer> writer(s);
                
                writer.StartObject();
                
                writer.String("type");
                writer.String("sim_data");
                    
                writer.String("content");
                writer.StartObject();
                
                writer.String("data");

                // Need to cast the selected var data
                writer.StartObject();
                for (auto& j : clients[i].selected_vars)
                {
                    // var name
                    writer.String(j.first.first->c_str());

                    // var data
                    const void *p = j.first.second;
                    // type of var
                    switch(j.second.first) {
                    case SV_INTEGER:
                        // size of var
                        switch(j.second.second) {
                        case 1: writer.Uint((unsigned)*(uint8_t*)p); break;
                        case 2: writer.Uint(*(uint16_t*)p); break;
                        case 4: writer.Uint64(*(uint32_t*)p); break;
                        case 8: writer.Uint64(*(uint64_t*)p); break;
                        default: writer.String("<invsize>"); break;
                        }
                        break;
                    case SV_FLOAT:
                        if(j.second.second == sizeof(float))
                            writer.Double(*(float*)p);
                        else
                            writer.Double(*(double*)p);
                        break;
                    }
                }
                writer.EndObject();

                writer.String("status");

                writer.StartObject();

                writer.String("sim");

                writer.Uint(status);

                writer.String("delay");

                writer.Double(delay);

                 writer.String("step");

                writer.Uint(step);

                writer.EndObject();
            
                writer.EndObject();
                
                writer.EndObject();
                
                // ManyMan frontend need newline character to determine when
                //   messages end.
                message = string(s.GetString()).append("\n");
                
                // Send sampled data to client
                if(send(clients[i].socket, message.c_str(), message.length(), 0) != (ssize_t) message.length())
                {
                    if(!m_quiet)
                    {
                        perror("! send sim_data error");
                    }
                }
            }
        }

        

#if defined(HAVE_NANOSLEEP)
        nanosleep(&m_tsdelay, 0);
#elif defined(HAVE_USLEEP)
        usleep(m_tsdelay.tv_sec * 1000000 + m_tsdelay.tv_usec);
#else
#error No sub-microsecond wait available on this system.
#endif

    }
}