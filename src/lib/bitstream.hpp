#pragma once

struct Bitstream_Reader {
private:
    size_t bit_offset_;
    uint8_t get_current_byte();
    unsigned get_zero_bit_count();
public:
    std::span<uint8_t> buffer;
    Bitstream_Reader(std::span<uint8_t> buffer, size_t byte_offset = 0);
    bool is_aligned() const;
    void align();
    bool can_read_bytes(size_t count) const;
    uint64_t read_bytes(unsigned byte_count);
    uint64_t read_bits(unsigned bit_count);
    size_t find_byte_sequence(uint8_t byte_sequence...) const;
    uint64_t read_exponential_golomb();
    int64_t read_signed_exponential_golomb();
    void discard(unsigned bit_count);
    size_t bit_offset() const;
    size_t byte_offset() const;
    void set_byte_offset(size_t new_byte_offset);
    bool finished() const;
};