#ifndef FT_DOIP_ENDPOINT_IMPLH
#define FT_DOIP_ENDPOINT_IMPLH


#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include "endpoint.h"
const std::uint32_t MESSAGE_SIZE_UNLIMITED = (std::numeric_limits<std::uint32_t>::max)();
template<typename Protocol>
class endpoint_impl : public endpoint {
    typedef typename Protocol::endpoint endpoint_type;
public:
    endpoint_impl(
        const endpoint_type& _local,
        boost::asio::io_service &_io)
    : local_(_local), 
      service_(_io){}

    virtual ~endpoint_impl(){}

    virtual bool is_client() const = 0;

    virtual void receive() = 0;

    void increment_use_count();
    void decrement_use_count();
    uint32_t get_use_count();

protected:
    std::atomic<uint32_t> use_count_;
    endpoint_type local_;
    boost::asio::io_service &service_;
};


#endif