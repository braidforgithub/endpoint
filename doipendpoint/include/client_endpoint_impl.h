#ifndef FT_DOIP_CLIENT_ENDPOINT_IMPL_H_
#define FT_DOIP_CLIENT_ENDPOINT_IMPL_H_

#include <boost/array.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/utility.hpp>

#include "endpoint_impl.h"
#include "buffer.h"
template<typename Protocol>
class client_endpoint_impl: public endpoint_impl<Protocol>,
        public std::enable_shared_from_this<client_endpoint_impl<Protocol> > {
public:
    typedef typename Protocol::endpoint endpoint_type;
    typedef typename Protocol::socket socket_type;

    client_endpoint_impl(const endpoint_type& _local, const endpoint_type& _remote,
                         boost::asio::io_service &_io,
                         std::uint32_t _max_message_size);
    virtual ~client_endpoint_impl();

    bool send(const uint8_t *_data, uint32_t _size);
    virtual void stop();

    bool is_client() const;

    bool is_established() const;
    bool is_established_or_connected() const;
    void set_established(bool _established);
    void set_connected(bool _connected);
    virtual bool get_remote_address(boost::asio::ip::address &_address) const;
    virtual std::uint16_t get_remote_port() const;

    std::uint16_t get_local_port() const;
    virtual bool is_reliable() const = 0;

    void connect_cbk(boost::system::error_code const &_error);
    void wait_connect_cbk(boost::system::error_code const &_error);
    virtual void send_cbk(boost::system::error_code const &_error, std::size_t _bytes);

    virtual void connect() = 0;
    virtual void receive() = 0;

protected:
    enum class cei_state_e : std::uint8_t {
        CLOSED,
        CONNECTING,
        CONNECTED,
        ESTABLISHED
    };
    void shutdown_and_close_socket(bool _recreate_socket);
    void shutdown_and_close_socket_unlocked(bool _recreate_socket);
    void start_connect_timer();

    mutable std::mutex socket_mutex_;
    std::unique_ptr<socket_type> socket_;
    const endpoint_type remote_;

    boost::asio::steady_timer flush_timer_;

    std::mutex connect_timer_mutex_;
    boost::asio::steady_timer connect_timer_;
    std::atomic<uint32_t> connect_timeout_;
    std::atomic<cei_state_e> state_;
    std::atomic<std::uint32_t> reconnect_counter_;
    mutable std::mutex mutex_;

    std::atomic<bool> was_not_connected_;

    std::atomic<std::uint16_t> local_port_;

    boost::asio::io_service::strand strand_;

private:
    virtual void set_local_port() = 0;
    virtual std::string get_remote_information() const = 0;
    virtual std::uint32_t get_max_allowed_reconnects() const = 0;
};

#endif