#ifndef FT_DOIP_SERVER_ENDPOINT_IMPL_H_
#define FT_DOIP_SERVER_ENDPOINT_IMPL_H_

#include "endpoint_impl.h"

template<typename Protocol>
class server_endpoint_impl: public endpoint_impl<Protocol>,
        public std::enable_shared_from_this<server_endpoint_impl<Protocol> > {
public:
    typedef typename Protocol::socket socket_type;
    typedef typename Protocol::endpoint endpoint_type;

    server_endpoint_impl(
        endpoint_type _local, 
        boost::asio::io_service &_io) 
    : endpoint_impl<Protocol>(_local, _io),
                          sent_timer_(_io),
                          is_sending_(false) {}

    virtual ~server_endpoint_impl(){}
    bool is_client() const;
    bool send(const uint8_t *_data, uint32_t _size);
    void send_cbk(boost::system::error_code const &_error, std::size_t _bytes);
protected:
    boost::asio::steady_timer sent_timer_;
    bool is_sending_;
    mutable std::mutex mutex_;
    std::mutex sent_mutex_;
};

#endif