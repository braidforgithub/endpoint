#ifndef FT_DOIP_ENDPOINT_MANAGER_IMPL_HPP_
#define FT_DOIP_ENDPOINT_MANAGER_IMPL_HPP_


#include <boost/asio/io_service.hpp>
#include "endpoint.h"
class endpoint_manager_impl
    : public std::enable_shared_from_this<endpoint_manager_impl> {
public:
    endpoint_manager_impl(boost::asio::io_service& _io);
    ~endpoint_manager_impl() = default;

    std::shared_ptr<endpoint> create_server_endpoint(std::string& ip, uint16_t _port, bool _reliable);

    std::shared_ptr<endpoint> find_server_endpoint(std::string& ip, uint16_t _port, bool _reliable) const;
    
    bool remove_server_endpoint(std::string& ip, uint16_t _port, bool _reliable);

    std::shared_ptr<endpoint> create_client_endpoint(std::string& ip, uint16_t _port, 
        std::string& _remote_ip, uint16_t _remote_port, bool _reliable);

    std::shared_ptr<endpoint> find_client_endpoint(std::string& ip, uint16_t _port, bool _reliable) const;
    
    bool remove_client_endpoint(std::string& ip, uint16_t _port, bool _reliable);
    
private:
    mutable std::recursive_mutex endpoint_mutex_;
    boost::asio::io_service& io_;
    typedef std::map<std::string, std::map<uint16_t,
                std::map<bool, std::shared_ptr<endpoint>>>> endpoints_t;
    endpoints_t server_endpoints_;
    endpoints_t client_endpoints_;
};

#endif