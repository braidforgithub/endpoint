#ifndef FT_DOIP_TCP_SERVER_ENDPOINT_IMPL_H_
#define FT_DOIP_TCP_SERVER_ENDPOINT_IMPL_H_

#include <map>
#include <memory>

#include <boost/asio/ip/tcp.hpp>
#include "buffer.h"
#include "server_endpoint_impl.h"

typedef server_endpoint_impl<
            boost::asio::ip::tcp
        > tcp_server_endpoint_base_impl;

class tcp_server_endpoint_impl: public tcp_server_endpoint_base_impl {

public:
    tcp_server_endpoint_impl(const endpoint_type& _local,
                             boost::asio::io_service &_io);
    virtual ~tcp_server_endpoint_impl(){}

    void start();
    void stop();
    void receive();

private:
    class connection: public std::enable_shared_from_this<connection> {

    public:
        typedef std::shared_ptr<connection> ptr;

        static ptr create(const std::weak_ptr<tcp_server_endpoint_impl>& _server,
                          std::uint32_t _max_message_size,
                          std::uint32_t _buffer_shrink_threshold,
                          boost::asio::io_service & _io_service,
                          std::chrono::milliseconds _send_timeout);
        socket_type & get_socket();
        std::unique_lock<std::mutex> get_socket_lock();

        void start();
        void stop();
        void receive();
        void send_data(const uint8_t *_data, uint32_t _size);
        void receive_cbk(boost::system::error_code const &_error,
                         std::size_t _bytes);
        void set_remote_info(const endpoint_type &_remote);
        const std::string get_address_port_local() const;
        void handle_recv_buffer_exception(const std::exception &_e);
        const std::string get_address_port_remote() const;
    private:
        connection(const std::weak_ptr<tcp_server_endpoint_impl>& _server,
                   std::uint32_t _max_message_size,
                   std::uint32_t _recv_buffer_size_initial,
                   std::uint32_t _buffer_shrink_threshold,
                   boost::asio::io_service & _io_service,
                   std::chrono::milliseconds _send_timeout);


        std::mutex socket_mutex_;
        tcp_server_endpoint_impl::socket_type socket_;
        std::weak_ptr<tcp_server_endpoint_impl> server_;

        const uint32_t max_message_size_;
        const uint32_t recv_buffer_size_initial_;

        message_buffer_t recv_buffer_;
        size_t recv_buffer_size_;
        std::uint32_t missing_capacity_;
        std::uint32_t shrink_count_;
        const std::uint32_t buffer_shrink_threshold_;

        endpoint_type remote_;
        boost::asio::ip::address remote_address_;
        std::uint16_t remote_port_;
        const std::chrono::milliseconds send_timeout_;
        std::size_t write_completion_condition(
                const boost::system::error_code& _error,
                std::size_t _bytes_transferred, std::size_t _bytes_to_send,
                const std::chrono::steady_clock::time_point _start);
        void stop_and_remove_connection();
        void wait_until_sent(const boost::system::error_code &_error);
    };

    std::mutex acceptor_mutex_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::mutex connections_mutex_;
    typedef std::map<endpoint_type, connection::ptr> connections_t;
    connections_t connections_;
    const std::uint32_t buffer_shrink_threshold_;
    const std::uint16_t local_port_;
    const std::chrono::milliseconds send_timeout_;
    void accept_cbk(const connection::ptr& _connection,
                    boost::system::error_code const &_error);
    void remove_connection(connection *_connection);
    
    
    
};

#endif