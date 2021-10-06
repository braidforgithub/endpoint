#include "udp_client_endpoint_impl.h"
#include <iomanip>
#include <sstream>
#include <iostream>
#include <boost/asio/ip/multicast.hpp>

udp_client_endpoint_impl::udp_client_endpoint_impl(
        const endpoint_type& _local,
        const endpoint_type& _remote,
        boost::asio::io_service &_io)
    : udp_client_endpoint_base_impl(_local,
                                    _remote, _io, 1000),
      remote_address_(_remote.address()),
      remote_port_(_remote.port()),
      udp_receive_buffer_size_(1000) {
}

udp_client_endpoint_impl::~udp_client_endpoint_impl() {
}

bool udp_client_endpoint_impl::is_local() const {
    return false;
}

void udp_client_endpoint_impl::connect() {
    std::lock_guard<std::mutex> its_lock(socket_mutex_);
    boost::system::error_code its_error;
    socket_->open(remote_.protocol(), its_error);
    if (!its_error || its_error == boost::asio::error::already_open) {
        // Enable SO_REUSEADDR to avoid bind problems with services going offline
        // and coming online again and the user has specified only a small number
        // of ports in the clients section for one service instance
        socket_->set_option(boost::asio::socket_base::reuse_address(true), its_error);
        if (its_error) {
            std::cout << "udp_client_endpoint_impl::connect: couldn't enable "
                    << "SO_REUSEADDR: " << its_error.message() << " remote:"
                    << get_address_port_remote();
        }
        socket_->set_option(boost::asio::socket_base::receive_buffer_size(
                udp_receive_buffer_size_), its_error);
        if (its_error) {
            std::cout << "udp_client_endpoint_impl::connect: couldn't set "
                    << "SO_RCVBUF: " << its_error.message() << " to: "
                    << std::dec << udp_receive_buffer_size_ << " remote:"
                    << get_address_port_remote();
        } else {
            boost::asio::socket_base::receive_buffer_size its_option;
            socket_->get_option(its_option, its_error);
            if (its_error) {
                std::cout << "udp_client_endpoint_impl::connect: couldn't get "
                        << "SO_RCVBUF: " << its_error.message() << " remote:"
                        << get_address_port_remote();
            } else {
                std::cout << "udp_client_endpoint_impl::connect: SO_RCVBUF is: "
                        << std::dec << its_option.value();
            }
        }

        // Bind address and, optionally, port.
        boost::system::error_code its_bind_error;
        socket_->bind(local_, its_bind_error);
        if(its_bind_error) {
            std::cout << "udp_client_endpoint::connect: "
                    "Error binding socket: " << its_bind_error.message()
                    << " remote:" << get_address_port_remote();
            try {
                // don't connect on bind error to avoid using a random port
                strand_.post(std::bind(&client_endpoint_impl::connect_cbk,
                                shared_from_this(), its_bind_error));
            } catch (const std::exception &e) {
                std::cout << "udp_client_endpoint_impl::connect: "
                        << e.what() << " remote:" << get_address_port_remote();
            }
            return;
        }

        state_ = cei_state_e::CONNECTING;
        socket_->async_connect(
            remote_,
            strand_.wrap(
                std::bind(
                    &udp_client_endpoint_base_impl::connect_cbk,
                    shared_from_this(),
                    std::placeholders::_1
                )
            )
        );
    } else {
        std::cout << "udp_client_endpoint::connect: Error opening socket: "
                << its_error.message() << " remote:" << get_address_port_remote();
        strand_.post(std::bind(&udp_client_endpoint_base_impl::connect_cbk,
                        shared_from_this(), its_error));
    }
}

void udp_client_endpoint_impl::start() {
    connect();
}

void udp_client_endpoint_impl::send_data(const uint8_t *_data, uint32_t _size) {

#if 1
    std::stringstream msg;
    msg << "ucei<" << remote_.address() << ":"
        << std::dec << remote_.port()  << ">::sq: ";
    for (std::size_t i = 0; i < _size; i++)
        msg << std::hex << std::setw(2) << std::setfill('0')
            << (int)(_data)[i] << " ";
    std::cout << msg.str();
#endif
    {
        std::lock_guard<std::mutex> its_lock(socket_mutex_);
        socket_->async_send(
            boost::asio::buffer(_data, _size),
            std::bind(
                &udp_client_endpoint_base_impl::send_cbk,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2
            )
        );
    }
}

void udp_client_endpoint_impl::receive() {
    std::lock_guard<std::mutex> its_lock(socket_mutex_);
    if (!socket_->is_open()) {
        return;
    }
    message_buffer_ptr_t its_buffer = std::make_shared<message_buffer_t>(1000);
    socket_->async_receive_from(
        boost::asio::buffer(*its_buffer),
        const_cast<endpoint_type&>(remote_),
        strand_.wrap(
            std::bind(
                &udp_client_endpoint_impl::receive_cbk,
                std::dynamic_pointer_cast<
                    udp_client_endpoint_impl
                >(shared_from_this()),
                std::placeholders::_1,
                std::placeholders::_2,
                its_buffer
            )
        )
    );
}

bool udp_client_endpoint_impl::get_remote_address(
        boost::asio::ip::address &_address) const {
    if (remote_address_.is_unspecified()) {
        return false;
    }
    _address = remote_address_;
    return true;
}

void udp_client_endpoint_impl::set_local_port() {
    std::lock_guard<std::mutex> its_lock(socket_mutex_);
    boost::system::error_code its_error;
    if (socket_->is_open()) {
        endpoint_type its_endpoint = socket_->local_endpoint(its_error);
        if (!its_error) {
            local_port_ = its_endpoint.port();
        } else {
            std::cout << "udp_client_endpoint_impl::set_local_port() "
                    << " couldn't get local_endpoint: " << its_error.message();
        }
    }
}

std::uint16_t udp_client_endpoint_impl::get_remote_port() const {
    return remote_port_;
}

void udp_client_endpoint_impl::receive_cbk(
        boost::system::error_code const &_error, std::size_t _bytes,
        const message_buffer_ptr_t& _recv_buffer) {
    if (_error == boost::asio::error::operation_aborted) {
        // endpoint was stopped
        return;
    }

#if 1
        std::stringstream msg;
        msg << "ucei::rcb(" << _error.message() << "): ";
        for (std::size_t i = 0; i < _bytes; ++i)
            msg << std::hex << std::setw(2) << std::setfill('0')
                << (int) (*_recv_buffer)[i] << " ";
        std::cout << msg.str();
#endif
    
    if (!_error) {
        receive();
    } else {
        if (_error == boost::asio::error::connection_refused) {
            shutdown_and_close_socket(false);
        } else {
            receive();
        }
    }
}

const std::string udp_client_endpoint_impl::get_address_port_remote() const {
    boost::system::error_code ec;
    std::string its_address_port;
    its_address_port.reserve(21);
    boost::asio::ip::address its_address;
    if (get_remote_address(its_address)) {
        its_address_port += its_address.to_string();
    }
    its_address_port += ":";
    its_address_port += std::to_string(remote_port_);
    return its_address_port;
}

const std::string udp_client_endpoint_impl::get_address_port_local() const {
    std::string its_address_port;
    its_address_port.reserve(21);
    boost::system::error_code ec;
    if (socket_->is_open()) {
        endpoint_type its_local_endpoint = socket_->local_endpoint(ec);
        if (!ec) {
            its_address_port += its_local_endpoint.address().to_string(ec);
            its_address_port += ":";
            its_address_port.append(std::to_string(its_local_endpoint.port()));
        }
    }
    return its_address_port;
}

std::string udp_client_endpoint_impl::get_remote_information() const {
    boost::system::error_code ec;
    return remote_.address().to_string(ec) + ":"
            + std::to_string(remote_.port());
}

bool udp_client_endpoint_impl::is_reliable() const {
    return false;
}
