#ifndef MESSAGEPROCESSOR_H
# define MESSAGEPROCESSOR_H

#include "communicator.h"

#include "rapidjson/document.h"

#include <fnmatch.h>

#include <vector>
#include <map>


using namespace std;
using namespace rapidjson;


class MessageProcessor
{
    typedef void (MessageProcessor::*func_ptr)(client *,
                                               rapidjson::Value&);
    
    map<string, func_ptr> processMap;
    
    vector<string>        known_msg_types;
    
    Simulator::MGSystem&  m_sys;
    CommandLineReader*    m_clr;
    
    void process_client_init(client * client,
                             rapidjson::Value& content);
    void process_selection_new(client * client,
                               rapidjson::Value& content);
    void process_selection_send(client * client,
                                rapidjson::Value& content);
    void process_pause_sim(client * client,
                           rapidjson::Value& content);
    void process_resume_sim(client * client,
                            rapidjson::Value& content);
    void process_set_step(client * client,
                          rapidjson::Value& content);
    void process_change_delay(client * client,
                              rapidjson::Value& content);
    
    void send_server_init(client * client);
    void send_selection_set(client * client);
    void send_invalid(client * client, string error);
    
public:
    struct timespec m_tsdelay;
    
    MessageProcessor(Simulator::MGSystem& sys, CommandLineReader* clr);

    void process(client * client, string msg); 
};

#endif