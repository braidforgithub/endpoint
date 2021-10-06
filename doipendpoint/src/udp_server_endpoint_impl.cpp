#include <boost/asio/ip/multicast.hpp>
#include "udp_server_endpoint_impl.h"

#include <iostream>
#define FT_DOIP_MAX_UDP_MESSAGE_SIZE 0xffffffff
#define UDP_RECV_BUFFER_SIZE 0x10000

udp_server_endpoint_impl::udp_server_endpoint_impl(
        const endpoint_type& _local,
        boost::asio::io_service &_io) :
        udp_server_endpoint_base_impl(_local, _io),
        unicast_socket_(_io, _local.protocol()),
        unicast_recv_buffer_(FT_DOIP_MAX_UDP_MESSAGE_SIZE, 0),
        local_port_(_local.port()) {
    boost::system::error_code ec;

    boost::asio::socket_base::reuse_address optionReuseAddress(true);
    unicast_socket_.set_option(optionReuseAddress, ec);
    boost::asio::detail::throw_error(ec, "reuse address");

    unicast_socket_.bind(_local, ec);
    boost::asio::detail::throw_error(ec, "bind");

    if (local_.address().is_v4()) {
        boost::asio::ip::multicast::outbound_interface option(_local.address().to_v4());
        unicast_socket_.set_option(option, ec);
        boost::asio::detail::throw_error(ec, "outbound interface option IPv4");
    } else if (local_.address().is_v6()) {
        boost::asio::ip::multicast::outbound_interface option(
                static_cast<unsigned int>(local_.address().to_v6().scope_id()));
        unicast_socket_.set_option(option, ec);
        boost::asio::detail::throw_error(ec, "outbound interface option IPv6");
    }

    boost::asio::socket_base::broadcast option(true);
    unicast_socket_.set_option(option, ec);
    boost::asio::detail::throw_error(ec, "broadcast option");

    const std::uint32_t its_udp_recv_buffer_size = UDP_RECV_BUFFER_SIZE;
    unicast_socket_.set_option(boost::asio::socket_base::receive_buffer_size(
            its_udp_recv_buffer_size), ec);

    if (ec) {
        std::cout << "udp_server_endpoint_impl:: couldn't set "
                << "SO_RCVBUF: " << ec.message() << " to: " << std::dec
                << its_udp_recv_buffer_size << " local port: " << std::dec
                << local_port_;
    } else {
        boost::asio::socket_base::receive_buffer_size its_option;
        unicast_socket_.get_option(its_option, ec);
        if (ec) {
            std::cout << "udp_server_endpoint_impl: couldn't get "
                    << "SO_RCVBUF: " << ec.message() << " local port:"
                    << std::dec << local_port_;
        } else {
            std::cout << "udp_server_endpoint_impl: SO_RCVBUF is: "
                    << std::dec << its_option.value();
        }
    }
}

void udp_server_endpoint_impl::start(){
    receive();
}

void udp_server_endpoint_impl::stop(){
    server_endpoint_impl::stop();
    {
        std::lock_guard<std::mutex> its_lock(unicast_mutex_);

        if (unicast_socket_.is_open()) {
            boost::system::error_code its_error;
            unicast_socket_.shutdown(socket_type::shutdown_both, its_error);
            unicast_socket_.close(its_error);
        }
    }

    {
        std::lock_guard<std::mutex> its_lock(multicast_mutex_);

        if (multicast_socket_ && multicast_socket_->is_open()) {
            boost::system::error_code its_error;
            multicast_socket_->shutdown(socket_type::shutdown_both, its_error);
            multicast_socket_->close(its_error);
        }
    }
}

void udp_server_endpoint_impl::receive(){
    receive_unicast();
}

void udp_server_endpoint_impl::receive_unicast() {

    std::lock_guard<std::mutex> its_lock(unicast_mutex_);

    if(unicast_socket_.is_open()) {
        unicast_socket_.async_receive_from(
                boost::asio::buffer(&unicast_recv_buffer_[0], 0xffffffff),
            unicast_remote_,
            std::bind(
                &udp_server_endpoint_impl::on_unicast_received,
                std::dynamic_pointer_cast<
                    udp_server_endpoint_impl >(shared_from_this()),
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3
            )
        );
    }
}

void udp_server_endpoint_impl::on_unicast_received(
        boost::system::error_code const &_error,
        std::size_t _bytes,
        boost::asio::ip::address const &_destination) {

    if (_error != boost::asio::error::operation_aborted) {
        {
            // By locking the multicast mutex here it is ensured that unicast
            // & multicast messages are not processed in parallel. This aligns
            // the behavior of endpoints with one and two active sockets.
            std::lock_guard<std::mutex> its_lock(multicast_mutex_);
            on_message_received(_error, _bytes, _destination,
                    unicast_remote_, unicast_recv_buffer_);
        }
        receive_unicast();
    }
}

void udp_server_endpoint_impl::on_message_received(
        boost::system::error_code const &_error, std::size_t _bytes,
        boost::asio::ip::address const &_destination,
        endpoint_type const &_remote,
        message_buffer_t const &_buffer) {
#if 1
    std::stringstream msg;
    msg << "usei::rcb(" << _error.message() << "): ";
    for (std::size_t i = 0; i < _bytes; ++i)
        msg << std::hex << std::setw(2) << std::setfill('0')
            << (int) _buffer[i] << " ";
    std::cout << msg.str();
#endif
}

void udp_server_endpoint_impl::send_data(const uint8_t *_data, uint32_t _size, endpoint_type &_remote){
    std::lock_guard<std::mutex> its_lock(unicast_mutex_);

    unicast_socket_.async_send_to(
        boost::asio::buffer(_data, _size),
        _remote,
        std::bind(
            &udp_server_endpoint_base_impl::send_cbk,
            shared_from_this(),
            std::placeholders::_1,
            std::placeholders::_2
        )
    );
}


