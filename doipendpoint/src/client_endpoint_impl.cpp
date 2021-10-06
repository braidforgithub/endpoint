#include "client_endpoint_impl.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <limits>
#include <iostream>

#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/local/stream_protocol.hpp>

#define FTDOIP_DEFAULT_CONNECT_TIMEOUT 1000

template<typename Protocol>
client_endpoint_impl<Protocol>::client_endpoint_impl(
        const endpoint_type& _local,
        const endpoint_type& _remote,
        boost::asio::io_service &_io,
        std::uint32_t _max_message_size)
        : endpoint_impl<Protocol>(_local, _io,
                _max_message_size),
          socket_(new socket_type(_io)), remote_(_remote),
          connect_timer_(_io),
          connect_timeout_(FTDOIP_DEFAULT_CONNECT_TIMEOUT), // TODO: use config variable
          state_(cei_state_e::CLOSED),
          reconnect_counter_(0),
          was_not_connected_(false),
          local_port_(0),
          strand_(_io) {
}

template<typename Protocol>
client_endpoint_impl<Protocol>::~client_endpoint_impl() {
}

template<typename Protocol>
bool client_endpoint_impl<Protocol>::is_client() const {
    return true;
}

template<typename Protocol>
bool client_endpoint_impl<Protocol>::is_established() const {
    return state_ == cei_state_e::ESTABLISHED;
}

template<typename Protocol>
bool client_endpoint_impl<Protocol>::is_established_or_connected() const {
    return (state_ == cei_state_e::ESTABLISHED
            || state_ == cei_state_e::CONNECTED);
}

template<typename Protocol>
void client_endpoint_impl<Protocol>::set_established(bool _established) {
    if (_established) {
        if (state_ != cei_state_e::CONNECTING) {
            std::lock_guard<std::mutex> its_lock(socket_mutex_);
            if (socket_->is_open()) {
                state_ = cei_state_e::ESTABLISHED;
            } else {
                state_ = cei_state_e::CLOSED;
            }
        }
    } else {
        state_ = cei_state_e::CLOSED;
    }
}

template<typename Protocol>
void client_endpoint_impl<Protocol>::set_connected(bool _connected) {
    if (_connected) {
        std::lock_guard<std::mutex> its_lock(socket_mutex_);
        if (socket_->is_open()) {
            state_ = cei_state_e::CONNECTED;
        } else {
            state_ = cei_state_e::CLOSED;
        }
    } else {
        state_ = cei_state_e::CLOSED;
    }
}

template<typename Protocol>
void client_endpoint_impl<Protocol>::stop() {
    {
        std::lock_guard<std::mutex> its_lock(connect_timer_mutex_);
        boost::system::error_code ec;
        connect_timer_.cancel(ec);
    }
    connect_timeout_ = FTDOIP_DEFAULT_CONNECT_TIMEOUT;
    shutdown_and_close_socket(false);
}


template<typename Protocol>
bool client_endpoint_impl<Protocol>::send(const uint8_t *_data, uint32_t _size) {
    std::lock_guard<std::mutex> its_lock(mutex_);

#if 1
    std::stringstream msg;
    msg << "cei::send: ";
    for (uint32_t i = 0; i < _size; i++)
    msg << std::hex << std::setw(2) << std::setfill('0') << (int)_data[i] << " ";
    std::cout << msg.str();
#endif

    return this->send_data(_data, _size);
}

template<typename Protocol>
void client_endpoint_impl<Protocol>::connect_cbk(
        boost::system::error_code const &_error) {
    if (_error == boost::asio::error::operation_aborted
            || endpoint_impl<Protocol>::sending_blocked_) {
        // endpoint was stopped
        shutdown_and_close_socket(false);
        return;
    }
    
    {
        std::lock_guard<std::mutex> its_lock(connect_timer_mutex_);
        connect_timer_.cancel();
    }
    connect_timeout_ = FTDOIP_DEFAULT_CONNECT_TIMEOUT; // TODO: use config variable
    reconnect_counter_ = 0;
    set_local_port();
    if (was_not_connected_) {
        was_not_connected_ = false;
    }

    receive();
}

template<typename Protocol>
void client_endpoint_impl<Protocol>::wait_connect_cbk(
        boost::system::error_code const &_error) {
    if (!_error && !client_endpoint_impl<Protocol>::sending_blocked_) {
        connect();
    }
}

template<typename Protocol>
void client_endpoint_impl<Protocol>::send_cbk(
        boost::system::error_code const &_error, std::size_t _bytes) {
    (void)_bytes;
    if (_error == boost::asio::error::broken_pipe) {
        state_ = cei_state_e::CLOSED;
        {
            std::lock_guard<std::mutex> its_lock(mutex_);
            
                std::cout << "cei::send_cbk received error: "
                        << _error.message() << " (" << std::dec
                        << _error.value() << ") " << get_remote_information();
        }
        
        was_not_connected_ = true;
        shutdown_and_close_socket(true);
        connect();
    } else if (_error == boost::asio::error::not_connected
            || _error == boost::asio::error::bad_descriptor
            || _error == boost::asio::error::no_permission) {
        state_ = cei_state_e::CLOSED;
        if (_error == boost::asio::error::no_permission) {
            std::cout << "cei::send_cbk received error: " << _error.message()
                    << " (" << std::dec << _error.value() << ") "
                    << get_remote_information();
            std::lock_guard<std::mutex> its_lock(mutex_);
        }
        was_not_connected_ = true;
        shutdown_and_close_socket(true);
        connect();
    } else if (_error == boost::asio::error::operation_aborted) {
        std::cout << "cei::send_cbk received error: " << _error.message();
        // endpoint was stopped
        endpoint_impl<Protocol>::sending_blocked_ = true;
        shutdown_and_close_socket(false);
    } else if (_error == boost::system::errc::destination_address_required) {
        std::cout << "cei::send_cbk received error: " << _error.message()
                << " (" << std::dec << _error.value() << ") "
                << get_remote_information();
        was_not_connected_ = true;
    } else {
        std::cout << "cei::send_cbk received error: " << _error.message()
                << " (" << std::dec << _error.value() << ") "
                << get_remote_information();
    }
}

template<typename Protocol>
void client_endpoint_impl<Protocol>::shutdown_and_close_socket(bool _recreate_socket) {
    std::lock_guard<std::mutex> its_lock(socket_mutex_);
    shutdown_and_close_socket_unlocked(_recreate_socket);
}

template<typename Protocol>
void client_endpoint_impl<Protocol>::shutdown_and_close_socket_unlocked(bool _recreate_socket) {
    local_port_ = 0;
    if (socket_->is_open()) {

        if (-1 == fcntl(socket_->native_handle(), F_GETFD)) {
            std::cout << "cei::shutdown_and_close_socket_unlocked: socket/handle closed already '"
                    << std::string(std::strerror(errno))
                    << "' (" << errno << ") " << get_remote_information();
        }

        boost::system::error_code its_error;
        socket_->shutdown(Protocol::socket::shutdown_both, its_error);
        socket_->close(its_error);
    }
    if (_recreate_socket) {
        socket_.reset(new socket_type(endpoint_impl<Protocol>::service_));
    }
}

template<typename Protocol>
bool client_endpoint_impl<Protocol>::get_remote_address(
        boost::asio::ip::address &_address) const {
    (void)_address;
    return false;
}

template<typename Protocol>
std::uint16_t client_endpoint_impl<Protocol>::get_remote_port() const {
    return 0;
}

template<typename Protocol>
std::uint16_t client_endpoint_impl<Protocol>::get_local_port() const {
    return local_port_;
}

template<typename Protocol>
void client_endpoint_impl<Protocol>::start_connect_timer() {
    std::lock_guard<std::mutex> its_lock(connect_timer_mutex_);
    connect_timer_.expires_from_now(
            std::chrono::milliseconds(connect_timeout_));
    connect_timer_.async_wait(
            std::bind(&client_endpoint_impl<Protocol>::wait_connect_cbk,
                      this->shared_from_this(), std::placeholders::_1));
}
