#include "server_endpoint_impl.h"
#include <iostream>

template<typename Protocol>
bool server_endpoint_impl<Protocol>::is_client() const{
    return false;
}

template<typename Protocol>
bool server_endpoint_impl<Protocol>::send(const uint8_t *_data, uint32_t _size){
    return this->send_data(_data, _size);
}

template<typename Protocol>
void server_endpoint_impl<Protocol>::send_cbk(boost::system::error_code const &_error, std::size_t _bytes){
    (void)_bytes;

    {
        std::lock_guard<std::mutex> its_sent_lock(sent_mutex_);
        is_sending_ = false;

        boost::system::error_code ec;
        sent_timer_.cancel(ec);
    }

    std::lock_guard<std::mutex> its_lock(mutex_);

    if(_error){
        std::cout << "sei::send_cbk received error: " << _error.message()
                << " (" << std::dec << _error.value() << ") "
                << std::endl;
    }
}