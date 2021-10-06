#ifndef FT_DOIP_UDP_SERVER_ENDPOINT_IMPL_H_
#define FT_DOIP_UDP_SERVER_ENDPOINT_IMPL_H_

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp_ext.hpp>
#include <atomic>
#include "server_endpoint_impl.h"
#include "buffer.h"
#include <iomanip>
typedef server_endpoint_impl<
            boost::asio::ip::udp_ext
        > udp_server_endpoint_base_impl;

class udp_server_endpoint_impl: public udp_server_endpoint_base_impl {
public:
    udp_server_endpoint_impl(const endpoint_type& _local,
                             boost::asio::io_service &_io);
    virtual ~udp_server_endpoint_impl(){}

    void start();
    void stop();

    void receive();
    void send_data(const uint8_t *_data, uint32_t _size, endpoint_type &_remote);
private:
    void set_broadcast();
    void receive_unicast();
    void receive_multicast(uint8_t _id);
    void on_unicast_received(boost::system::error_code const &_error,
            std::size_t _bytes,
            boost::asio::ip::address const &_destination);
    void on_message_received(boost::system::error_code const &_error,
                     std::size_t _bytes,
                     boost::asio::ip::address const &_destination,
                     endpoint_type const &_remote,
                     message_buffer_t const &_buffer);
    socket_type unicast_socket_;
    endpoint_type unicast_remote_;
    message_buffer_t unicast_recv_buffer_;
    mutable std::mutex unicast_mutex_;
    mutable std::mutex multicast_mutex_;

    std::unique_ptr<socket_type> multicast_socket_;
    std::unique_ptr<endpoint_type> multicast_local_;
    const std::uint16_t local_port_;
};

#endif
