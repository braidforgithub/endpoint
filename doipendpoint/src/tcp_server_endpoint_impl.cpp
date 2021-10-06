#include "tcp_server_endpoint_impl.h"
#include <iostream>
#include <boost/asio/write.hpp>
#include <boost/asio.hpp>
#include <iomanip>
#define FTDOIP_HEADER_SIZE 4


tcp_server_endpoint_impl::tcp_server_endpoint_impl(const endpoint_type& _local,
        boost::asio::io_service &_io)
        :tcp_server_endpoint_base_impl(
        _local, 
        _io),
        acceptor_(_io),
        buffer_shrink_threshold_(1000),
        local_port_(_local.port()),
        send_timeout_(2000){
    boost::system::error_code ec;
    acceptor_.open(_local.protocol(), ec);
    boost::asio::detail::throw_error(ec, "acceptor open");
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    boost::asio::detail::throw_error(ec, "acceptor set_option");
    acceptor_.bind(_local, ec);
    boost::asio::detail::throw_error(ec, "acceptor bind");
    acceptor_.listen(boost::asio::socket_base::max_connections, ec);
    boost::asio::detail::throw_error(ec, "acceptor listen");
}

void tcp_server_endpoint_impl::start(){
    std::lock_guard<std::mutex> its_lock(acceptor_mutex_);
    if (acceptor_.is_open()) {
        connection::ptr new_connection = connection::create(
                std::dynamic_pointer_cast<tcp_server_endpoint_impl>(
                        shared_from_this()), 0xffffffff,
                        buffer_shrink_threshold_,
                        service_,
                        send_timeout_);

        {
            std::unique_lock<std::mutex> its_socket_lock(new_connection->get_socket_lock());
            acceptor_.async_accept(new_connection->get_socket(),
                    std::bind(&tcp_server_endpoint_impl::accept_cbk,
                            std::dynamic_pointer_cast<tcp_server_endpoint_impl>(
                                    shared_from_this()), new_connection,
                            std::placeholders::_1));
        }
    }
}

void tcp_server_endpoint_impl::stop(){
    server_endpoint_impl::stop();
    {
        std::lock_guard<std::mutex> its_lock(acceptor_mutex_);
        if(acceptor_.is_open()) {
            boost::system::error_code its_error;
            acceptor_.close(its_error);
        }
    }
    {
        std::lock_guard<std::mutex> its_lock(connections_mutex_);
        for (const auto &c : connections_) {
            c.second->stop();
        }
        connections_.clear();
    }
}

void tcp_server_endpoint_impl::remove_connection(
        tcp_server_endpoint_impl::connection *_connection) {
    std::lock_guard<std::mutex> its_lock(connections_mutex_);
    for (auto it = connections_.begin(); it != connections_.end();) {
        if (it->second.get() == _connection) {
            it = connections_.erase(it);
            break;
        } else {
            ++it;
        }
    }
}

void tcp_server_endpoint_impl::receive() {
    // intentionally left empty
}

void tcp_server_endpoint_impl::accept_cbk(const connection::ptr& _connection,
        boost::system::error_code const &_error) {

    if (!_error) {
        boost::system::error_code its_error;
        endpoint_type remote;
        {
            std::unique_lock<std::mutex> its_socket_lock(_connection->get_socket_lock());
            socket_type &new_connection_socket = _connection->get_socket();
            remote = new_connection_socket.remote_endpoint(its_error);
            _connection->set_remote_info(remote);
            // Nagle algorithm off
            new_connection_socket.set_option(boost::asio::ip::tcp::no_delay(true), its_error);

            new_connection_socket.set_option(boost::asio::socket_base::keep_alive(true), its_error);
            if (its_error) {
                std::cout << "tcp_server_endpoint::connect: couldn't enable "
                        << "keep_alive: " << its_error.message();
            }
        }
        if (!its_error) {
            {
                std::lock_guard<std::mutex> its_lock(connections_mutex_);
                connections_[remote] = _connection;
            }
            _connection->start();
        }
    }
    if (_error != boost::asio::error::bad_descriptor
            && _error != boost::asio::error::operation_aborted
            && _error != boost::asio::error::no_descriptors) {
        start();
    } else if (_error == boost::asio::error::no_descriptors) {
        std::cout<< "tcp_server_endpoint_impl::accept_cbk: "
        << _error.message() << " (" << std::dec << _error.value()
        << ") Will try to accept again in 1000ms";
        std::shared_ptr<boost::asio::steady_timer> its_timer =
        std::make_shared<boost::asio::steady_timer>(service_,
                std::chrono::milliseconds(1000));
        auto its_ep = std::dynamic_pointer_cast<tcp_server_endpoint_impl>(
                shared_from_this());
        its_timer->async_wait([its_timer, its_ep]
                               (const boost::system::error_code& _error) {
            if (!_error) {
                its_ep->start();
            }
        });
    }
}

tcp_server_endpoint_impl::connection::connection(const std::weak_ptr<tcp_server_endpoint_impl>& _server,
        std::uint32_t _max_message_size,
        std::uint32_t _recv_buffer_size_initial,
        std::uint32_t _buffer_shrink_threshold,
        boost::asio::io_service & _io_service,
        std::chrono::milliseconds _send_timeout):
        socket_(_io_service),
        server_(_server),
        max_message_size_(_max_message_size),
        recv_buffer_size_initial_(_recv_buffer_size_initial),
        recv_buffer_(_recv_buffer_size_initial, 0),
        recv_buffer_size_(0),
        missing_capacity_(0),
        shrink_count_(0),
        buffer_shrink_threshold_(_buffer_shrink_threshold),
        remote_port_(0),
        send_timeout_(_send_timeout){

}

void tcp_server_endpoint_impl::connection::set_remote_info(
        const endpoint_type &_remote) {
    remote_ = _remote;
    remote_address_ = _remote.address();
    remote_port_ = _remote.port();
}

const std::string tcp_server_endpoint_impl::connection::get_address_port_remote() const {
    std::string its_address_port;
    its_address_port.reserve(21);
    boost::system::error_code ec;
    its_address_port += remote_address_.to_string(ec);
    its_address_port += ":";
    its_address_port += std::to_string(remote_port_);
    return its_address_port;
}

const std::string tcp_server_endpoint_impl::connection::get_address_port_local() const {
    std::string its_address_port;
    its_address_port.reserve(21);
    boost::system::error_code ec;
    if (socket_.is_open()) {
        endpoint_type its_local_endpoint = socket_.local_endpoint(ec);
        if (!ec) {
            its_address_port += its_local_endpoint.address().to_string(ec);
            its_address_port += ":";
            its_address_port += std::to_string(its_local_endpoint.port());
        }
    }
    return its_address_port;
}

void tcp_server_endpoint_impl::connection::handle_recv_buffer_exception(
        const std::exception &_e) {
    std::stringstream its_message;
    its_message <<"tcp_server_endpoint_impl::connection catched exception"
            << _e.what() << " local: " << get_address_port_local()
            << " remote: " << get_address_port_remote()
            << " shutting down connection. Start of buffer: ";

    for (std::size_t i = 0; i < recv_buffer_size_ && i < 16; i++) {
        its_message << std::setw(2) << std::setfill('0') << std::hex
            << (int) (recv_buffer_[i]) << " ";
    }

    its_message << " Last 16 Bytes captured: ";
    for (int i = 15; recv_buffer_size_ > 15 && i >= 0; i--) {
        its_message << std::setw(2) << std::setfill('0') << std::hex
            <<  (int) (recv_buffer_[static_cast<size_t>(i)]) << " ";
    }
    std::cout << its_message.str();
    recv_buffer_.clear();
    if (socket_.is_open()) {
        boost::system::error_code its_error;
        socket_.shutdown(socket_.shutdown_both, its_error);
        socket_.close(its_error);
    }
    std::shared_ptr<tcp_server_endpoint_impl> its_server = server_.lock();
    if (its_server) {
        its_server->remove_connection(this);
    }
}

tcp_server_endpoint_impl::connection::ptr 
tcp_server_endpoint_impl::connection::create(const std::weak_ptr<tcp_server_endpoint_impl>& _server,
        std::uint32_t _max_message_size,
        std::uint32_t _buffer_shrink_threshold,
        boost::asio::io_service & _io_service,
        std::chrono::milliseconds _send_timeout){

    const std::uint32_t its_initial_receveive_buffer_size =
            FTDOIP_HEADER_SIZE;
    return ptr(new connection(_server, _max_message_size,
            its_initial_receveive_buffer_size,
            _buffer_shrink_threshold,
            _io_service, _send_timeout));
}

tcp_server_endpoint_impl::socket_type &
tcp_server_endpoint_impl::connection::get_socket() {
    return socket_;
}

std::unique_lock<std::mutex>
tcp_server_endpoint_impl::connection::get_socket_lock() {
    return std::unique_lock<std::mutex>(socket_mutex_);
}

void tcp_server_endpoint_impl::connection::start() {
    receive();
}

void tcp_server_endpoint_impl::connection::receive() {
    std::lock_guard<std::mutex> its_lock(socket_mutex_);
    if(socket_.is_open()) {
        const std::size_t its_capacity(recv_buffer_.capacity());
        size_t buffer_size = its_capacity - recv_buffer_size_;
        try {
            if (missing_capacity_) {
                if (missing_capacity_ > MESSAGE_SIZE_UNLIMITED) {
                    std::cout << "Missing receive buffer capacity exceeds allowed maximum!";
                    return;
                }
                const std::size_t its_required_capacity(recv_buffer_size_ + missing_capacity_);
                if (its_capacity < its_required_capacity) {
                    recv_buffer_.reserve(its_required_capacity);
                    recv_buffer_.resize(its_required_capacity, 0x0);
                    if (recv_buffer_.size() > 1048576) {
                        std::cout << "tse: recv_buffer size is: " <<
                                recv_buffer_.size()
                                << " local: " << get_address_port_local()
                                << " remote: " << get_address_port_remote();
                    }
                }
                buffer_size = missing_capacity_;
                missing_capacity_ = 0;
            } else if (buffer_shrink_threshold_
                    && shrink_count_ > buffer_shrink_threshold_
                    && recv_buffer_size_ == 0) {
                recv_buffer_.resize(recv_buffer_size_initial_, 0x0);
                recv_buffer_.shrink_to_fit();
                buffer_size = recv_buffer_size_initial_;
                shrink_count_ = 0;
            }
        } catch (const std::exception &e) {
            handle_recv_buffer_exception(e);
            // don't start receiving again
            return;
        }
        socket_.async_receive(boost::asio::buffer(&recv_buffer_[recv_buffer_size_], buffer_size),
                std::bind(&tcp_server_endpoint_impl::connection::receive_cbk,
                        shared_from_this(), std::placeholders::_1,
                        std::placeholders::_2));
    }
}

void tcp_server_endpoint_impl::connection::stop() {
    std::lock_guard<std::mutex> its_lock(socket_mutex_);
    if (socket_.is_open()) {
        boost::system::error_code its_error;
        socket_.shutdown(socket_.shutdown_both, its_error);
        socket_.close(its_error);
    }
}

void tcp_server_endpoint_impl::connection::send_data(
        const uint8_t *_data, uint32_t _size) {
    std::shared_ptr<tcp_server_endpoint_impl> its_server(server_.lock());
    if (!its_server) {
        std::cout << "tcp_server_endpoint_impl::connection::send_queued "
                " couldn't lock server_";
        return;
    }

    {
        std::lock_guard<std::mutex> its_lock(socket_mutex_);
        {
            std::lock_guard<std::mutex> its_sent_lock(its_server->sent_mutex_);
            its_server->is_sending_ = true;
        }

        boost::asio::async_write(socket_, boost::asio::buffer(_data, _size),
                 std::bind(&tcp_server_endpoint_impl::connection::write_completion_condition,
                           shared_from_this(),
                           std::placeholders::_1,
                           std::placeholders::_2,
                           _size,
                           std::chrono::steady_clock::now()),
                std::bind(&tcp_server_endpoint_base_impl::send_cbk,
                          its_server,
                          std::placeholders::_1,
                          std::placeholders::_2));
    }
}

std::size_t
tcp_server_endpoint_impl::connection::write_completion_condition(
        const boost::system::error_code& _error,
        std::size_t _bytes_transferred, std::size_t _bytes_to_send,
        const std::chrono::steady_clock::time_point _start) {
    if (_error) {
        std::cout << "tse::write_completion_condition: "
                << _error.message() << "(" << std::dec << _error.value()
                << ") bytes transferred: " << std::dec << _bytes_transferred
                << " bytes to sent: " << std::dec << _bytes_to_send << " "
                << "remote:" << get_address_port_remote() << std::endl;

        stop_and_remove_connection();
        return 0;
    }
    return _bytes_to_send - _bytes_transferred;   
}

void tcp_server_endpoint_impl::connection::stop_and_remove_connection() {
    std::shared_ptr<tcp_server_endpoint_impl> its_server(server_.lock());
    if (!its_server) {
        std::cout << "tse::connection::stop_and_remove_connection "
                " couldn't lock server_";
        return;
    }
    {
        std::lock_guard<std::mutex> its_lock(its_server->connections_mutex_);
        stop();
    }
    its_server->remove_connection(this);
}

void tcp_server_endpoint_impl::connection::receive_cbk(
        boost::system::error_code const &_error,
        std::size_t _bytes) {
    if (_error == boost::asio::error::operation_aborted) {
        // endpoint was stopped
        return;
    }
    std::shared_ptr<tcp_server_endpoint_impl> its_server(server_.lock());
    if (!its_server) {
        std::cout << "tcp_server_endpoint_impl::connection::receive_cbk "
                " couldn't lock server_";
        return;
    }
#if 1
    std::stringstream msg;
    for (std::size_t i = 0; i < _bytes + recv_buffer_size_; ++i)
        msg << std::hex << std::setw(2) << std::setfill('0')
                << (int) recv_buffer_[i] << " ";
    std::cout << msg.str();
#else
    std::shared_ptr<routing_host> its_host = its_server->routing_host_.lock();
    if (its_host) {
        if (!_error && 0 < _bytes) {
            if (recv_buffer_size_ + _bytes < recv_buffer_size_) {
                std::cout << "receive buffer overflow in tcp client endpoint ~> abort!";
                return;
            }
            recv_buffer_size_ += _bytes;

            size_t its_iteration_gap = 0;
            bool has_full_message;
            do {
                uint64_t read_message_size
                    = utility::get_message_size(&recv_buffer_[its_iteration_gap],
                            recv_buffer_size_);
                if (read_message_size > MESSAGE_SIZE_UNLIMITED) {
                    std::cout << "Message size exceeds allowed maximum!";
                    return;
                }
                uint32_t current_message_size = static_cast<uint32_t>(read_message_size);
                has_full_message = (current_message_size > VSOMEIP_RETURN_CODE_POS
                                   && current_message_size <= recv_buffer_size_);
                if (has_full_message) {
                    bool needs_forwarding(true);
                    if (needs_forwarding) {
                        its_host->on_message(&recv_buffer_[its_iteration_gap],
                                current_message_size, its_server.get(),
                                boost::asio::ip::address(),
                                VSOMEIP_ROUTING_CLIENT,
                                std::make_pair(ANY_UID, ANY_GID),
                                remote_address_, remote_port_);
                    }
                    calculate_shrink_count();
                    missing_capacity_ = 0;
                    recv_buffer_size_ -= current_message_size;
                    its_iteration_gap += current_message_size;
                } else if (current_message_size > recv_buffer_size_) {
                    missing_capacity_ = current_message_size
                            - static_cast<std::uint32_t>(recv_buffer_size_);
                } else if (FTDOIP_SOMEIP_HEADER_SIZE > recv_buffer_size_) {
                    missing_capacity_ = FTDOIP_SOMEIP_HEADER_SIZE
                                - static_cast<std::uint32_t>(recv_buffer_size_);
                }
            } while (has_full_message && recv_buffer_size_);
            if (its_iteration_gap) {
                // Copy incomplete message to front for next receive_cbk iteration
                for (size_t i = 0; i < recv_buffer_size_; ++i) {
                    recv_buffer_[i] = recv_buffer_[i + its_iteration_gap];
                }
                // Still more capacity needed after shifting everything to front?
                if (missing_capacity_ &&
                        missing_capacity_ <= recv_buffer_.capacity() - recv_buffer_size_) {
                    missing_capacity_ = 0;
                }
            }
            receive();
        }
    }
    #endif
    if (_error == boost::asio::error::eof
            || _error == boost::asio::error::connection_reset
            || _error == boost::asio::error::timed_out) {
        if(_error == boost::asio::error::timed_out) {
            std::lock_guard<std::mutex> its_lock(socket_mutex_);
            std::cout << "tcp_server_endpoint receive_cbk: " << _error.message()
                    << " local: " << get_address_port_local()
                    << " remote: " << get_address_port_remote();
        }
        wait_until_sent(boost::asio::error::operation_aborted);
    }
}