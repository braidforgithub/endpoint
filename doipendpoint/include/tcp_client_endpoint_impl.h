#ifndef FT_DOIP_TCP_CLIENT_ENDPOINT_IMPL_H_
#define FT_DOIP_TCP_CLIENT_ENDPOINT_IMPL_H_

#include <chrono>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include "client_endpoint_impl.h"

typedef client_endpoint_impl<
            boost::asio::ip::tcp
        > tcp_client_endpoint_base_impl;

class tcp_client_endpoint_impl: public tcp_client_endpoint_base_impl {
public:
    tcp_client_endpoint_impl(const endpoint_type& _local,
                             const endpoint_type& _remote,
                             boost::asio::io_service &_io);
    virtual ~tcp_client_endpoint_impl();

    void start();
    bool get_remote_address(boost::asio::ip::address &_address) const;
    std::uint16_t get_remote_port() const;
    bool is_reliable() const;
    bool is_local() const;
    void send_cbk(boost::system::error_code const &_error, std::size_t _bytes,
                  const message_buffer_ptr_t& _sent_msg);
    void send_data(uint8_t *_data, uint32_t _size);

private:
    void receive_cbk(boost::system::error_code const &_error,
                     std::size_t _bytes,
                     const message_buffer_ptr_t&  _recv_buffer,
                     std::size_t _recv_buffer_size);

    void connect();
    void receive();
    void receive(message_buffer_ptr_t  _recv_buffer,
                 std::size_t _recv_buffer_size,
                 std::size_t _missing_capacity);
    void calculate_shrink_count(const message_buffer_ptr_t& _recv_buffer,
                                std::size_t _recv_buffer_size);
    const std::string get_address_port_remote() const;
    const std::string get_address_port_local() const;
    void handle_recv_buffer_exception(const std::exception &_e,
                                      const message_buffer_ptr_t& _recv_buffer,
                                      std::size_t _recv_buffer_size);
    void set_local_port();
    std::size_t write_completion_condition(
            const boost::system::error_code& _error,
            std::size_t _bytes_transferred, std::size_t _bytes_to_send,
            const std::chrono::steady_clock::time_point _start);
    std::string get_remote_information() const;

    void wait_until_sent(const boost::system::error_code &_error);
    std::uint32_t get_max_allowed_reconnects() const;
    const std::uint32_t recv_buffer_size_initial_;
    message_buffer_ptr_t recv_buffer_;
    std::uint32_t shrink_count_;
    const std::uint32_t buffer_shrink_threshold_;
    std::uint32_t tcp_connect_time_max_;

    const boost::asio::ip::address remote_address_;
    const std::uint16_t remote_port_;

    const std::chrono::milliseconds send_timeout_;
    std::chrono::steady_clock::time_point connect_timepoint_;

    std::mutex sent_mutex_;
    bool is_sending_;
    boost::asio::steady_timer sent_timer_;
};

#endif
