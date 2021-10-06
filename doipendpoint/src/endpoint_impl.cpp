#include "endpoint_impl.h"

template<typename Protocol>
void endpoint_impl<Protocol>::increment_use_count() {
    use_count_++;
}

template<typename Protocol>
void endpoint_impl<Protocol>::decrement_use_count() {
    if (use_count_ > 0)
        use_count_--;
}

template<typename Protocol>
uint32_t endpoint_impl<Protocol>::get_use_count() {
    return use_count_;
}
