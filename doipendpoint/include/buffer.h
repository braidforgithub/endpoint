#ifndef FT_DOIP_BUFFER_H_
#define FT_DOIP_BUFFER_H_

#include <vector>
#include <memory>

typedef uint8_t byte_t;
typedef std::vector<byte_t> message_buffer_t;
typedef std::shared_ptr<message_buffer_t> message_buffer_ptr_t;

#endif
