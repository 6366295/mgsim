#include "messageprocessor.h"
#include "sampling.h"
#include "cli/commands.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rapidjson/allocators.h"

#include <sys/socket.h>

#include <iostream>

MessageProcessor::MessageProcessor(Simulator::MGSystem& sys, CommandLineReader* clr)
    : m_sys(sys),
      m_clr(clr)
{     
    // Default send delay
    m_tsdelay.tv_sec = 1;
    m_tsdelay.tv_nsec = 0;

    // Message processor functions, for less if statements
    processMap["client_init"] = &MessageProcessor::process_client_init;
    processMap["selection_new"] = &MessageProcessor::process_selection_new;
    processMap["selection_send"] = &MessageProcessor::process_selection_send;
    processMap["pause_sim"] = &MessageProcessor::process_pause_sim;
    processMap["resume_sim"] = &MessageProcessor::process_resume_sim;
    processMap["set_step"] = &MessageProcessor::process_set_step;
    processMap["change_delay"] = &MessageProcessor::process_change_delay;
    
    // Add processMap key to known_msg_types
    for(auto i : processMap)
        known_msg_types.push_back(i.first);
}

void MessageProcessor::process(client * client, string msg)
{
    string  filtered_msg;
    bool    known_msg;
    size_t  pos;
    
    known_msg = false;
    
    // Parse JSON to Document
    Document doc;
    doc.Parse<0>(msg.c_str());
    
    // Check if parse is succesful
    if(doc.IsObject())
    {
        // Check if type and content exists in message and the format is correct
        if(doc.FindMember("type") != doc.MemberEnd() && 
           doc.FindMember("content") != doc.MemberEnd())
        {
            if(doc["type"].IsString() && doc["content"].IsObject())
            {
                // Check if message type exists
                for(auto type : known_msg_types)
                {
                    if (fnmatch(type.c_str(), doc["type"].GetString(), 0) == 0)
                    {
                        known_msg = true;
                        break;
                    }
                }
                
                // If message type exist
                if(known_msg)
                {
                    if(!client->initialized &&
                       (FNM_NOMATCH == fnmatch("client_init",
                                               doc["type"].GetString(), 0))
                      )
                    {
                        //TODO flag as logging
                        clog << "Did not recieve initialization message first" << endl;
                    }
                    else
                    {
                        // message type is already checked, process message
                        (this->*processMap[doc["type"].GetString()])(client,
                                                                     doc["content"]);
                    }
                }
                else
                {
                    send_invalid(client, "Received unknown message type");
                }
            }
            else
            {
                send_invalid(client, "Received unknown message format");
            }
        }
        else
        {
            send_invalid(client, "Received unknown message format");
        }
    }
    else
    {
        send_invalid(client, "Received a non-JSON format message");
    }
}

void MessageProcessor::process_client_init(client * client, Value& content)
{
    Value::ConstMemberIterator itr;
    vector<string> pats;
    
    if(!client->initialized)
    {
        itr = content.FindMember("name");
        
        // set client name
        if(itr->value.IsString())
        {
            client->name = itr->value.GetString();
        }
        else
        {
            send_invalid(client, "No member name in content of message");
        }
        
        // Get vars_vector of current selected vars
        pats = client->default_vars;
        // Always send kernel.cycle
        pats.insert(pats.begin(), "kernel.cycle");

        client->selected_vars = GetSelectedVariableSamples(pats);

        // client is now initialized
        client->initialized = true;
        
        send_server_init(client);
    }
    else
    {
        // TODO: flag as logging, or remove
        clog << "! Client already initialized" << endl;
    }
}

// TODO: IF NO MATCH FOUND, SEND INVALLIED, CHANGED FALSE
void MessageProcessor::process_selection_new(client * client, Value& content)
{
    Value::ConstMemberIterator  itr;
    vector<string>              pats;
    vars_vector                 temp;
    
    itr = content.FindMember("sample_vars");

    //change to lock?
    client->changed = true;

    // set client data seletion
    if(itr->value.IsArray())
    {
        for(SizeType i = 0; i < itr->value.Size(); i++){
            // TODO: logging
            //clog << itr->value[i].GetString() << endl;
            pats.push_back(itr->value[i].GetString());
        }

        // Always send kernel.cycle
        pats.insert(pats.begin(), "kernel.cycle");

        temp = GetSelectedVariableSamples(pats);
        if(temp.empty())
        {
            client->changed = false;
            send_invalid(client, "No match found");
        }
        else
        {
            client->selected_vars = temp;
            send_selection_set(client);
        }
    }
    else
    {
        send_invalid(client, "No member sample_vars in content of message");
    }
}

void MessageProcessor::process_selection_send(client * client, Value& content)
{
    client->changed = false;
}

void MessageProcessor::process_pause_sim(client * client, Value& content)
{
    m_sys.Abort();
}

void MessageProcessor::process_resume_sim(client * client, Value& content)
{
    if(m_clr->step == 0)
    {
        m_clr->resume = 0;
    }
    else
    {
        m_clr->resume = 2;
    }
}

void MessageProcessor::process_set_step(client * client, Value& content)
{
    Value::ConstMemberIterator itr;
    itr = content.FindMember("step");
    int step;

    if(itr->value.IsInt())
    {
        step = itr->value.GetInt();
        m_clr->step = step;
    }
    else
    {
        send_invalid(client, "No member step in content of message");
    }
}

void MessageProcessor::process_change_delay(client * client, Value& content)
{
    Value::ConstMemberIterator  itr;
    double delay;

    itr = content.FindMember("delay");

    if(itr->value.IsDouble())
    {
        delay = itr->value.GetDouble();


        if(delay > 10.0)
            delay = 10.0;
        else if(delay < 0.1)
            delay = 0.1;

        m_tsdelay.tv_sec = delay;
        m_tsdelay.tv_nsec = (delay - (float)m_tsdelay.tv_sec) * 1000000000.;
    }
    else
    {
        send_invalid(client, "No member delay in content of message");
    }
}





void MessageProcessor::send_server_init(client * client)
{
    string message;
    
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    
    // Create message
    writer.StartObject();
    
    writer.String("type");
    writer.String("server_init");
        
    writer.String("content");
    writer.StartObject();
    
    writer.String("name");
    writer.String("MGSim");
            
    writer.String("cores");
    writer.Uint(m_sys.GetConfig().getValue<Simulator::PSize>("NumProcessors"));
    
    writer.String("frequency");
    writer.Uint(m_sys.GetConfig().getValue<Simulator::PSize>("CoreFreq"));
    
    writer.String("sample_vars");

    writer.StartArray();
    for(auto& i : client->selected_vars)
    {
        if(fnmatch(i.first.first->c_str(), "kernel.cycle", 0) == 0)
            continue;
        // var name
        writer.String(i.first.first->c_str());
    }
    writer.EndArray();

    writer.String("default_vars");

    writer.StartArray();
    for(auto& i : client->default_vars)
    {
        // var name
        writer.String(i.c_str());
    }
    writer.EndArray();
    
    writer.EndObject();
    
    writer.EndObject();
    
    // ManyMan frontend needs newline character to know that the message is complete
    message = string(s.GetString()).append("\n");
    
    // send server_init message
    if(send(client->socket, message.c_str(), message.length(), 0) != (ssize_t) message.length())
    {
        perror("! send_server_init");
    }
}

void MessageProcessor::send_selection_set(client * client)
{
    string message;
    
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    
    // Create message
    writer.StartObject();
    
    writer.String("type");
    writer.String("selection_set");
        
    writer.String("content");
    writer.StartObject();
    
    writer.String("sample_vars");

    writer.StartArray();
    for(auto& i : client->selected_vars)
    {
        if(fnmatch(i.first.first->c_str(), "kernel.cycle", 0) == 0)
            continue;
        // var name
        writer.String(i.first.first->c_str());
    }
    writer.EndArray();
    
    writer.EndObject();
    
    writer.EndObject();
    
    // ManyMan frontend needs newline character to know that the message is complete
    message = string(s.GetString()).append("\n");
    
    // send server_init message
    if(send(client->socket, message.c_str(), message.length(), 0) != (ssize_t) message.length())
    {
        perror("! send_selection_set");
    }
}

void MessageProcessor::send_invalid(client * client, string error)
{
    string message;
    
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    
    // Create message
    writer.StartObject();
    
    writer.String("type");
    writer.String("invalid_message");
        
    writer.String("content");
    writer.StartObject();
    
    writer.String("message");
    writer.String(error.c_str());
   
    writer.EndObject();
    
    writer.EndObject();
    
    // ManyMan frontend needs newline character to know that the message is complete
    message = string(s.GetString()).append("\n");
    
    // send invalid_message message
    if(send(client->socket, message.c_str(), message.length(), 0) != (ssize_t) message.length())
    {
        perror("send_invalid");
    }
}