#ifndef COMMUNICATOR_H
# define COMMUNICATOR_H

#include <arch/MGSystem.h>
#include "cli/simreadline.h"

#include <pthread.h>
#include <string>
#include <vector>

#include "sampling.h"

#define MAXCLIENTS 10
#define MAXRCVLEN 500

namespace Simulator {
    class MGSystem;
}

class MessageProcessor;
class CommandLineReader;

// Data structure for each connected client
struct client
{
    int            socket = 0;
    bool           initialized = false;
    bool           changed = false;
    std::string    name = "";

    std::string    buffer;


    // Default samplevars
    std::vector<std::string>    default_vars = {"cpu*.pipeline*"};

    // Defined in sampling.h
    // Vector contains name, address, type and size of sampled vars
    vars_vector                 selected_vars;
};

class Communicator
{   
    Simulator::MGSystem&  m_sys;
    CommandLineReader*    m_clr;
    MessageProcessor*     m_msgprocessor;

    pthread_t             m_serverthread;
    pthread_t             m_senderthread;

    bool                  m_quiet;
    volatile bool         m_enabled;
    
    struct timespec       m_tsdelay;
    struct client         clients[MAXCLIENTS];

    int                   max_clients = MAXCLIENTS;
    int                   server_socket;
    int                   length;
    int                   pipe_fd[2];
    
    friend void* runserver(void*);
    friend void* runsender(void*);

    void server();
    void sender();
    
    int InitServer();
    
public:
    Communicator(Simulator::MGSystem& sys, CommandLineReader* clr, bool enabled, bool quiet);
    Communicator(const Communicator&) = delete;
    Communicator& operator=(const Communicator&) = delete;
    ~Communicator();
};

#endif
