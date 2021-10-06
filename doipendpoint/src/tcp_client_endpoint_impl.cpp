#include <iomanip>
#include <iostream>
#include <boost/asio/write.hpp>

#include "tcp_client_endpoint_impl.h"

#define MAX_RECONNECTS_UNLIMITED 10
#define FTDOIP_MAX_TCP_SENT_WAIT_TIME 1000
tcp_client_endpoint_impl::tcp_client_endpoint_impl(
        const endpoint_type& _local,
        const endpoint_type& _remote,
        boost::asio::io_service &_io)
    : tcp_client_endpoint_base_impl(_local,
                                    _remote, _io, 1000),
      recv_buffer_size_initial_(4),
      recv_buffer_(std::make_shared<message_buffer_t>(recv_buffer_size_initial_, 0)),
      shrink_count_(0),
      buffer_shrink_threshold_(1000),
      remote_address_(_remote.address()),
      remote_port_(_remote.port()),
      send_timeout_(1000),
      tcp_connect_time_max_(1000),
      sent_timer_(_io) {
}

tcp_client_endpoint_impl::~tcp_client_endpoint_impl() {
}

bool tcp_client_endpoint_impl::is_local() const {
    return false;
}

void tcp_client_endpoint_impl::start() {
    connect();
}

void tcp_client_endpoint_impl::connect() {
    std::lock_guard<std::mutex> its_lock(socket_mutex_);
    boost::system::error_code its_error;
    socket_->open(remote_.protocol(), its_error);

    if (!its_error || its_error == boost::asio::error::already_open) {
        // Nagle algorithm off
        socket_->set_option(boost::asio::ip::tcp::no_delay(true), its_error);
        if (its_error) {
            std::cout << "tcp_client_endpoint::connect: couldn't disable "
                    << "Nagle algorithm: " << its_error.message()
                    << " remote:" << get_address_port_remote();
        }

        socket_->set_option(boost::asio::socket_base::keep_alive(true), its_error);
        if (its_error) {
            std::cout << "tcp_client_endpoint::connect: couldn't enable "
                    << "keep_alive: " << its_error.message()
                    << " remote:" << get_address_port_remote();
        }

        // Enable SO_REUSEADDR to avoid bind problems with services going offline
        // and coming online again and the user has specified only a small number
        // of ports in the clients section for one service instance
        socket_->set_option(boost::asio::socket_base::reuse_address(true), its_error);
        if (its_error) {
            std::cout << "tcp_client_endpoint::connect: couldn't enable "
                    << "SO_REUSEADDR: " << its_error.message()
                    << " remote:" << get_address_port_remote();
        }
        socket_->set_option(boost::asio::socket_base::linger(true, 0), its_error);
        if (its_error) {
            std::cout << "tcp_client_endpoint::connect: couldn't enable "
                    << "SO_LINGER: " << its_error.message()
                    << " remote:" << get_address_port_remote();
        }

        // Bind address and, optionally, port.
        boost::system::error_code its_bind_error;
        socket_->bind(local_, its_bind_error);
        if(its_bind_error) {
            std::cout << "tcp_client_endpoint::connect: "
                    "Error binding socket: " << its_bind_error.message()
                    << " remote:" << get_address_port_remote();
            try {
                // don't connect on bind error to avoid using a random port
                strand_.post(std::bind(&client_endpoint_impl::connect_cbk,
                                shared_from_this(), its_bind_error));
            } catch (const std::exception &e) {
                std::cout << "tcp_client_endpoint_impl::connect: "
                        << e.what() << " remote:" << get_address_port_remote();
            }
            return;
        }

        state_ = cei_state_e::CONNECTING;
        connect_timepoint_ = std::chrono::steady_clock::now();

        socket_->async_connect(
            remote_,
            strand_.wrap(
                std::bind(
                    &tcp_client_endpoint_base_impl::connect_cbk,
                    shared_from_this(),
                    std::placeholders::_1
                )
            )
        );
    } else {
        std::cout << "tcp_client_endpoint::connect: Error opening socket: "
                << its_error.message() << " remote:" << get_address_port_remote();
        strand_.post(std::bind(&tcp_client_endpoint_base_impl::connect_cbk,
                                shared_from_this(), its_error));
    }
}

void tcp_client_endpoint_impl::receive() {
    message_buffer_ptr_t its_recv_buffer;
    {
        std::lock_guard<std::mutex> its_lock(socket_mutex_);
        its_recv_buffer = recv_buffer_;
    }
    receive(its_recv_buffer, 0, 0);
}

void tcp_client_endpoint_impl::receive(message_buffer_ptr_t  _recv_buffer,
             std::size_t _recv_buffer_size,
             std::size_t _missing_capacity) {
    std::lock_guard<std::mutex> its_lock(socket_mutex_);
    if(socket_->is_open()) {
        const std::size_t its_capacity(_recv_buffer->capacity());
        size_t buffer_size = its_capacity - _recv_buffer_size;
        try {
            if (_missing_capacity) {
                if (_missing_capacity > MESSAGE_SIZE_UNLIMITED) {
                    std::cout << "Missing receive buffer capacity exceeds allowed maximum!";
                    return;
                }
                const std::size_t its_required_capacity(_recv_buffer_size + _missing_capacity);
                if (its_capacity < its_required_capacity) {
                    _recv_buffer->reserve(its_required_capacity);
                    _recv_buffer->resize(its_required_capacity, 0x0);
                    if (_recv_buffer->size() > 1048576) {
                        std::cout << "tce: recv_buffer size is: " <<
                                _recv_buffer->size()
                                << " local: " << get_address_port_local()
                                << " remote: " << get_address_port_remote();
                    }
                }
                buffer_size = _missing_capacity;
            } else if (buffer_shrink_threshold_
                    && shrink_count_ > buffer_shrink_threshold_
                    && _recv_buffer_size == 0) {
                _recv_buffer->resize(recv_buffer_size_initial_, 0x0);
                _recv_buffer->shrink_to_fit();
                buffer_size = recv_buffer_size_initial_;
                shrink_count_ = 0;
            }
        } catch (const std::exception &e) {
            handle_recv_buffer_exception(e, _recv_buffer, _recv_buffer_size);
            // don't start receiving again
            return;
        }
        socket_->async_receive(
            boost::asio::buffer(&(*_recv_buffer)[_recv_buffer_size], buffer_size),
            strand_.wrap(
                std::bind(
                    &tcp_client_endpoint_impl::receive_cbk,
                    std::dynamic_pointer_cast< tcp_client_endpoint_impl >(shared_from_this()),
                    std::placeholders::_1,
                    std::placeholders::_2,
                    _recv_buffer,
                    _recv_buffer_size
                )
            )
        );
    }
}

void tcp_client_endpoint_impl::send_data(uint8_t *_data, uint32_t _size) {

#if 1
    std::stringstream msg;
    msg << "tcei<" << remote_.address() << ":"
        << std::dec << remote_.port()  << ">::sq: ";
    for (std::size_t i = 0; i < _size; i++)
        msg << std::hex << std::setw(2) << std::setfill('0')
            << (int)_data[i] << " ";
    std::cout << msg.str();
#endif
    {
        std::lock_guard<std::mutex> its_lock(socket_mutex_);
        if (socket_->is_open()) {
            {
                std::lock_guard<std::mutex> its_sent_lock(sent_mutex_);
                is_sending_ = true;
            }
            boost::asio::async_write(
                *socket_,
                boost::asio::buffer(_data, _size),
                std::bind(&tcp_client_endpoint_impl::write_completion_condition,
                          std::static_pointer_cast<tcp_client_endpoint_impl>(shared_from_this()),
                          std::placeholders::_1,
                          std::placeholders::_2,
                          _size,
                          std::chrono::steady_clock::now()),
                std::bind(
                    &tcp_client_endpoint_base_impl::send_cbk,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2
                )
            );
        }
    }
}


bool tcp_client_endpoint_impl::get_remote_address(
        boost::asio::ip::address &_address) const {
    if (remote_address_.is_unspecified()) {
        return false;
    }
    _address = remote_address_;
    return true;
}

void tcp_client_endpoint_impl::set_local_port() {
    std::lock_guard<std::mutex> its_lock(socket_mutex_);
    boost::system::error_code its_error;
    if (socket_->is_open()) {
        endpoint_type its_endpoint = socket_->local_endpoint(its_error);
        if (!its_error) {
            local_port_ = its_endpoint.port();
        } else {
            std::cout << "tcp_client_endpoint_impl::set_local_port() "
                    << " couldn't get local_endpoint: " << its_error.message();
        }
    }
}

std::size_t tcp_client_endpoint_impl::write_completion_condition(
        const boost::system::error_code& _error, std::size_t _bytes_transferred,
        std::size_t _bytes_to_send,
        const std::chrono::steady_clock::time_point _start) {

    if (_error) {
        std::cout << "tce::write_completion_condition: "
                << _error.message() << "(" << std::dec << _error.value()
                << ") bytes transferred: " << std::dec << _bytes_transferred
                << " bytes to sent: " << std::dec << _bytes_to_send << " "
                << "remote:" << get_address_port_remote();
        return 0;
    }

    return _bytes_to_send - _bytes_transferred;
}

std::uint16_t tcp_client_endpoint_impl::get_remote_port() const {
    return remote_port_;
}

bool tcp_client_endpoint_impl::is_reliable() const {
  return true;
}

void tcp_client_endpoint_impl::receive_cbk(
        boost::system::error_code const &_error, std::size_t _bytes,
        const message_buffer_ptr_t& _recv_buffer, std::size_t _recv_buffer_size) {
    if (_error == boost::asio::error::operation_aborted) {
        // endpoint was stopped
        return;
    }
#if 1
    std::stringstream msg;
    msg << "cei::rcb (" << _error.message() << "): ";
    for (std::size_t i = 0; i < _bytes + _recv_buffer_size; ++i)
        msg << std::hex << std::setw(2) << std::setfill('0')
            << (int) (*_recv_buffer)[i] << " ";
    std::cout << msg.str();
#endif
}

void tcp_client_endpoint_impl::calculate_shrink_count(const message_buffer_ptr_t& _recv_buffer,
                                                      std::size_t _recv_buffer_size) {
    if (buffer_shrink_threshold_) {
        if (_recv_buffer->capacity() != recv_buffer_size_initial_) {
            if (_recv_buffer_size < (_recv_buffer->capacity() >> 1)) {
                shrink_count_++;
            } else {
                shrink_count_ = 0;
            }
        }
    }
}

const std::string tcp_client_endpoint_impl::get_address_port_remote() const {
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

const std::string tcp_client_endpoint_impl::get_address_port_local() const {
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

void tcp_client_endpoint_impl::handle_recv_buffer_exception(
        const std::exception &_e,
        const message_buffer_ptr_t& _recv_buffer,
        std::size_t _recv_buffer_size) {
    boost::system::error_code ec;

    std::stringstream its_message;
    its_message <<"tcp_client_endpoint_impl::connection catched exception"
            << _e.what() << " local: " << get_address_port_local()
            << " remote: " << get_address_port_remote()
            << " shutting down connection. Start of buffer: ";

    for (std::size_t i = 0; i < _recv_buffer_size && i < 16; i++) {
        its_message << std::setw(2) << std::setfill('0') << std::hex
            << (int) ((*_recv_buffer)[i]) << " ";
    }

    its_message << " Last 16 Bytes captured: ";
    for (int i = 15; _recv_buffer_size > 15 && i >= 0; i--) {
        its_message << std::setw(2) << std::setfill('0') << std::hex
            << (int) ((*_recv_buffer)[static_cast<size_t>(i)]) << " ";
    }
    std::cout << its_message.str();
    _recv_buffer->clear();

    {
        std::lock_guard<std::mutex> its_lock(connect_timer_mutex_);
        boost::system::error_code ec;
        connect_timer_.cancel(ec);
    }
    if (socket_->is_open()) {
        boost::system::error_code its_error;
        socket_->shutdown(socket_type::shutdown_both, its_error);
        socket_->close(its_error);
    }
}

std::string tcp_client_endpoint_impl::get_remote_information() const {
    boost::system::error_code ec;
    return remote_.address().to_string(ec) + ":"
            + std::to_string(remote_.port());
}

void tcp_client_endpoint_impl::send_cbk(boost::system::error_code const &_error,
                                        std::size_t _bytes,
                                        const message_buffer_ptr_t& _sent_msg) {
    (void)_bytes;

    {
        // Signal that the current send operation has finished.
        // Note: Waiting is always done after having closed the socket.
        //       Therefore, no new send operation will be scheduled.
        std::lock_guard<std::mutex> its_sent_lock(sent_mutex_);
        is_sending_ = false;

        boost::system::error_code ec;
        sent_timer_.cancel(ec);
    }

    if (_error == boost::system::errc::destination_address_required) {
        std::cout << "tce::send_cbk received error: " << _error.message()
                << " (" << std::dec << _error.value() << ") "
                << get_remote_information();
        was_not_connected_ = true;
    } else if (_error == boost::asio::error::operation_aborted) {
        // endpoint was stopped
        shutdown_and_close_socket(false);
    } else {
        if (state_ == cei_state_e::CONNECTING) {
            std::cout << "tce::send_cbk endpoint is already restarting:"
                    << get_remote_information();
        } else {
            state_ = cei_state_e::CONNECTING;
            shutdown_and_close_socket(false);
        }
        
        std::cout << "tce::send_cbk received error: "
                << _error.message() << " (" << std::dec
                << _error.value() << ") " << get_remote_information();
    }
}

std::uint32_t tcp_client_endpoint_impl::get_max_allowed_reconnects() const {
    return MAX_RECONNECTS_UNLIMITED;
}

void tcp_client_endpoint_impl::wait_until_sent(const boost::system::error_code &_error) {

    std::unique_lock<std::mutex> its_sent_lock(sent_mutex_);
    if (!is_sending_ || !_error) {
        its_sent_lock.unlock();
        if (!_error)
            std::cout << __func__
                << ": Maximum wait time for send operation exceeded for tce.";
    } else {
        std::chrono::milliseconds its_timeout(FTDOIP_MAX_TCP_SENT_WAIT_TIME);
        boost::system::error_code ec;
        sent_timer_.expires_from_now(its_timeout, ec);
        sent_timer_.async_wait(std::bind(&tcp_client_endpoint_impl::wait_until_sent,
                std::dynamic_pointer_cast<tcp_client_endpoint_impl>(shared_from_this()),
                std::placeholders::_1));
    }
}