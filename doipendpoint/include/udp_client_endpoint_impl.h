#ifndef FT_DOIP_UDP_CLIENT_ENDPOINT_IMPL_H_
#define FT_DOIP_UDP_CLIENT_ENDPOINT_IMPL_H_

#include <memory>

#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/udp.hpp>
#include "client_endpoint_impl.h"

typedef client_endpoint_impl<
            boost::asio::ip::udp
        > udp_client_endpoint_base_impl;

class udp_client_endpoint_impl: virtual public udp_client_endpoint_base_impl {

public:
    udp_client_endpoint_impl(const endpoint_type& _local,
                             const endpoint_type& _remote,
                             boost::asio::io_service &_io);
    virtual ~udp_client_endpoint_impl();

    void start();

    void receive_cbk(boost::system::error_code const &_error,
                     std::size_t _bytes, const message_buffer_ptr_t& _recv_buffer);

    bool get_remote_address(boost::asio::ip::address &_address) const;
    std::uint16_t get_remote_port() const;
    bool is_local() const;
    bool is_reliable() const;

private:
    void send_data(const uint8_t *_data, uint32_t _size);
    void connect();
    void receive();
    void set_local_port();
    const std::string get_address_port_remote() const;
    const std::string get_address_port_local() const;
    std::string get_remote_information() const;

    const boost::asio::ip::address remote_address_;
    const std::uint16_t remote_port_;
    const std::uint32_t udp_receive_buffer_size_;
};
#endif