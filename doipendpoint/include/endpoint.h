#ifndef FT_DOIP_ENDPOINT_H
#define FT_DOIP_ENDPOINT_H
#include <cstdint>
#include <boost/asio/ip/address.hpp>

class endpoint{
public:
    virtual ~endpoint() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool send(const uint8_t *_data, uint32_t _size) = 0;
    virtual void receive() = 0;
    virtual void increment_use_count() = 0;
    virtual void decrement_use_count() = 0;
    virtual uint32_t get_use_count() = 0;
    
};
#endif