#include "endpoint_manager_impl.h"
#include "tcp_server_endpoint_impl.h"
#include "udp_server_endpoint_impl.h"
#include "tcp_client_endpoint_impl.h"

#include <iostream>
endpoint_manager_impl::endpoint_manager_impl(boost::asio::io_service& _io):
        io_(_io){
}

std::shared_ptr<endpoint> 
endpoint_manager_impl::create_server_endpoint(std::string& ip, uint16_t _port, bool _reliable){
    std::lock_guard<std::recursive_mutex> its_lock(endpoint_mutex_);
    std::shared_ptr<endpoint> its_endpoint;
    try {
        boost::asio::ip::address its_unicast = boost::asio::ip::address::from_string(ip);
        
        if (_reliable) {
            its_endpoint = std::make_shared<tcp_server_endpoint_impl>(
                    boost::asio::ip::tcp::endpoint(its_unicast, _port),
                    io_);
        } else {
            its_endpoint = std::make_shared<udp_server_endpoint_impl>(
                    boost::asio::ip::udp::endpoint(its_unicast, _port),
                    io_);
        }
        
        if (its_endpoint) {
            server_endpoints_[ip][_port][_reliable] = its_endpoint;
            its_endpoint->start();
        }
    } catch (const std::exception &e) {
        std::cout << __func__
                << " Server endpoint creation failed."
                << " Reason: "<< e.what()
                << " Port: " << _port
                << " (reliable="
                << (_reliable ? "reliable" : "unreliable")
                << ")";
    }

    return (its_endpoint);
}

std::shared_ptr<endpoint> 
endpoint_manager_impl::find_server_endpoint(std::string& _ip, uint16_t _port, bool _reliable) const{
    std::shared_ptr<endpoint> its_endpoint;
    std::lock_guard<std::recursive_mutex> its_lock(endpoint_mutex_);
    auto found_ip = server_endpoints_.find(_ip);
    if(found_ip != server_endpoints_.end()){
        auto found_port = found_ip->second.find(_port);
        if (found_port != found_ip->second.end()) {
            auto found_endpoint = found_port->second.find(_reliable);
            if (found_endpoint != found_port->second.end()) {
                its_endpoint = found_endpoint->second;
            }
        }
    }
    
    return its_endpoint;
}

bool 
endpoint_manager_impl::remove_server_endpoint(std::string& _ip, uint16_t _port, bool _reliable){
    bool ret = false;
    std::lock_guard<std::recursive_mutex> its_lock(endpoint_mutex_);
    auto found_ip = server_endpoints_.find(_ip);
    if(found_ip != server_endpoints_.end()){
        auto found_port = found_ip->second.find(_port);
        if (found_port != found_ip->second.end()) {
            auto found_reliable = found_port->second.find(_reliable);
            if (found_reliable != found_port->second.end()) {
                if (found_reliable->second->get_use_count() == 0 &&
                        found_port->second.erase(_reliable)) {
                    ret = true;
                    if (found_port->second.empty()) {
                        found_ip->second.erase(found_port);
                    }
                    if (found_ip->second.empty()){
                        server_endpoints_.erase(found_ip);
                    }
                }
            }
        }
    }
    
    return ret;
}

std::shared_ptr<endpoint> 
endpoint_manager_impl::create_client_endpoint(std::string& _ip, uint16_t _port, 
                                                std::string& _remote_ip, uint16_t _remote_port, bool _reliable){
    std::lock_guard<std::recursive_mutex> its_lock(endpoint_mutex_);
    std::shared_ptr<endpoint> its_endpoint;
    try {
        boost::asio::ip::address its_unicast = boost::asio::ip::address::from_string(_ip);
        boost::asio::ip::address remote_unicast = boost::asio::ip::address::from_string(_remote_ip);
        if (_reliable) {
            its_endpoint = std::make_shared<tcp_client_endpoint_impl>(
                    boost::asio::ip::tcp::endpoint(its_unicast, _port),
                    boost::asio::ip::tcp::endpoint(remote_unicast, _remote_port),
                    io_);
        } else {
            #if 0
            its_endpoint = std::make_shared<udp_client_endpoint_impl>(
                    boost::asio::ip::udp::endpoint(its_unicast, _port),
                    boost::asio::ip::tcp::endpoint(remote_unicast, _remote_port),
                    io_);
            #endif
        }
        
        if (its_endpoint) {
            client_endpoints_[_ip][_port][_reliable] = its_endpoint;
            its_endpoint->start();
        }
    } catch (const std::exception &e) {
        std::cout << __func__
                << " Client endpoint creation failed."
                << " Reason: "<< e.what()
                << " Port: " << _port
                << " (reliable="
                << (_reliable ? "reliable" : "unreliable")
                << ")";
    }

    return its_endpoint;
}

std::shared_ptr<endpoint> 
endpoint_manager_impl::find_client_endpoint(std::string& _ip, uint16_t _port, bool _reliable) const{
    std::shared_ptr<endpoint> its_endpoint;
    std::lock_guard<std::recursive_mutex> its_lock(endpoint_mutex_);
    auto found_ip = client_endpoints_.find(_ip);
    if(found_ip != client_endpoints_.end()){
        auto found_port = found_ip->second.find(_port);
        if (found_port != found_ip->second.end()) {
            auto found_endpoint = found_port->second.find(_reliable);
            if (found_endpoint != found_port->second.end()) {
                its_endpoint = found_endpoint->second;
            }
        }
    }
    
    return its_endpoint;
}

bool 
endpoint_manager_impl::remove_client_endpoint(std::string& _ip, uint16_t _port, bool _reliable){
    bool ret = false;
    std::lock_guard<std::recursive_mutex> its_lock(endpoint_mutex_);
    auto found_ip = client_endpoints_.find(_ip);
    if(found_ip != client_endpoints_.end()){
        auto found_port = found_ip->second.find(_port);
        if (found_port != found_ip->second.end()) {
            auto found_reliable = found_port->second.find(_reliable);
            if (found_reliable != found_port->second.end()) {
                if (found_reliable->second->get_use_count() == 0 &&
                        found_port->second.erase(_reliable)) {
                    ret = true;
                    if (found_port->second.empty()) {
                        found_ip->second.erase(found_port);
                    }
                    if (found_ip->second.empty()){
                        client_endpoints_.erase(found_ip);
                    }
                }
            }
        }
    }
    
    return ret;
}