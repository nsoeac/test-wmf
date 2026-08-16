#include "bitstream.hpp"

#include "type.hpp"

uint8_t Bitstream_Reader::get_current_byte() {
    size_t byte_index = byte_offset();
    assert(byte_index < buffer.size());
    uint8_t byte_value = buffer[byte_index];
    return byte_value;
}

unsigned Bitstream_Reader::get_zero_bit_count() {
    unsigned zero_bit_count = 0;
    while (read_bits(1) == 0) {
        zero_bit_count += 1;
    }

    return zero_bit_count;
}

Bitstream_Reader::Bitstream_Reader(std::span<uint8_t> buffer, size_t byte_offset) :
    buffer(buffer),
    bit_offset_(byte_offset * 8) {}

bool Bitstream_Reader::is_aligned() const {
    bool is_aligned = (bit_offset_ % 8) == 0;
    return is_aligned;
}

void Bitstream_Reader::align() {
    if (!is_aligned()) {
        bit_offset_ = (bit_offset_ / 8) * 8;
    }
}

bool Bitstream_Reader::can_read_bytes(size_t count) const {
    size_t requested_byte_index = byte_offset() + count;
    bool within_bounds = requested_byte_index <= buffer.size();
    return within_bounds;
}

uint64_t Bitstream_Reader::read_bytes(unsigned byte_count) {
    assert(byte_count <= sizeof(uint64_t));
    assert(is_aligned());

    unsigned bytes_read = 0;
    uint64_t result = 0;
    while (bytes_read < byte_count) {
        size_t byte_value = get_current_byte();
        bit_offset_ += CHAR_BIT;
        bytes_read += 1;
        unsigned left_shift = (byte_count - bytes_read) * CHAR_BIT;
        uint64_t value_to_add = byte_value << left_shift;
        result += value_to_add;
    }

    return result;
}

uint64_t Bitstream_Reader::read_bits(unsigned bit_count) {
    assert(bit_count <= (sizeof(uint64_t) * CHAR_BIT));

    unsigned bits_read = 0;
    uint64_t result = 0;
    while (bits_read < bit_count) {
        unsigned remaining_bit_count = bit_count - bits_read;
        unsigned offset_in_byte = bit_offset_ % CHAR_BIT;
        unsigned remaining_bit_count_in_byte = CHAR_BIT - offset_in_byte;
        unsigned bit_count_to_read = std::min<unsigned>(remaining_bit_count, remaining_bit_count_in_byte);
        unsigned right_shift = CHAR_BIT - bit_count_to_read;
        unsigned left_shift = right_shift - offset_in_byte;

        uint8_t right_shifted = UCHAR_MAX >> right_shift;
        uint8_t left_shifted = right_shifted << left_shift;
        uint8_t byte_value = get_current_byte();
        uint8_t masked_unshifted_byte = byte_value & left_shifted;
        uint8_t byte = masked_unshifted_byte >> left_shift;

        bits_read += bit_count_to_read;
        bit_offset_ += bit_count_to_read;
        unsigned result_shift = bit_count - bits_read;
        uint64_t value_to_add = (uint64_t)byte << result_shift;
        result += value_to_add;
    }

    return result;
}

size_t Bitstream_Reader::find_byte_sequence(uint8_t byte_sequence...) const {
    Bitstream_Reader inner_reader = *this;

    while (inner_reader.can_read_bytes(pack_size(byte_sequence))) {
        size_t start_byte_offset = inner_reader.byte_offset();

        bool all_match = true;
        for (uint8_t requested_byte : { byte_sequence }) {
            uint8_t actual_byte = (uint8_t)inner_reader.read_bytes(1);
            if (requested_byte != actual_byte) {
                all_match = false;
                break;
            }

            if (all_match) {
                return start_byte_offset;
            }
        }
    }

    return buffer.size();
}

uint64_t Bitstream_Reader::read_exponential_golomb() {
    unsigned zero_bit_count = get_zero_bit_count();
    uint64_t result = 1llu << zero_bit_count;
    if (zero_bit_count > 0) {
        unsigned left_shift = zero_bit_count - 1;

        while (left_shift > 0) {
            uint64_t bit = read_bits(1);
            uint64_t shifted_bit = bit << left_shift;
            left_shift -= 1;
            result += shifted_bit;
        }
    }

    return result;
}

int64_t Bitstream_Reader::read_signed_exponential_golomb() {
    uint64_t unsigned_result = read_exponential_golomb();
    if ((unsigned_result % 2) == 0) {
        int64_t result = -(int64_t)(unsigned_result / 2);
        return result;
    } else {
        int64_t result = (unsigned_result + 1) / 2;
        return result;
    }
}

void Bitstream_Reader::discard(unsigned bit_count) {
    bit_offset_ += bit_count;
}

size_t Bitstream_Reader::bit_offset() const {
    return bit_offset_;
}

size_t Bitstream_Reader::byte_offset() const {
    return bit_offset_ / 8;
}

void Bitstream_Reader::set_byte_offset(size_t new_byte_offset) {
    size_t new_bit_offset = new_byte_offset * CHAR_BIT;
    bit_offset_ = new_bit_offset;
}

bool Bitstream_Reader::finished() const {
    assert(byte_offset() <= buffer.size());
    return byte_offset() == buffer.size();
}