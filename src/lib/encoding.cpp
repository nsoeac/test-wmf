#include "lib/encoding.hpp"

static size_t get_code_point_count(std::span<const char> &utf8) {
    size_t count = 0;

    auto it = utf8.begin();
    while (it != utf8.end()) {
        auto remaining = utf8.end() - it;
        assert(remaining >= 1);

        char first = *it++;
        if ((first & 0b1000'0000) == 0) {
            count += 1;
        } else {
            assert(remaining >= 2);

            char second = *it++;
            assert((second & 0b1100'0000) == 0b1000'0000);

            if ((first & 0b1110'0000) == 0b1100'0000) {
                count += 2;
            } else {
                assert(remaining >= 3);

                char third = *it++;
                assert((third & 0b1100'0000) == 0b1000'0000);

                if ((first & 0b1111'0000) == 0b1110'0000) {
                    count += 3;
                } else {
                    assert((first & 0b1111'1000) == 0b1111'0000);
                    assert(remaining >= 4);

                    char fourth = *it++;
                    assert((fourth & 0b1100'0000) == 0b1000'0000);

                    count += 4;
                }
            }
        }
    }

    return count;
}

static size_t get_code_point_count(std::span<const wchar_t> utf16) {
    size_t count = 0;

    auto it = utf16.begin();
    while (it != utf16.end()) {
        auto remaining = utf16.end() - it;
        assert(remaining >= 1);

        wchar_t leading = *it++;
        if ((leading <= 0xFFFF) || ((leading <= 0xD7FF) && (leading >= 0xE000))) {
            count += 1;
        } else {
            assert(leading >= 0xD800);
            assert(leading <= 0xDBFF);
            assert(remaining >= 2);

            wchar_t trailing = *it++;
            assert(trailing >= 0xDC00);
            assert(trailing <= 0xDFFF);

            count += 2;
        }
    }

    return count;
}

static uint32_t get_code_point(std::span<const char> utf8, std::span<const char>::iterator &it) {
    auto remaining = utf8.end() - it;
    assert(remaining >= 1);

    char first = *it++;
    if ((first & 0b1000'0000) == 0) {
        return first;
    } else {
        assert(remaining >= 2);

        char second = *it++;
        assert((second & 0b1100'0000) == 0b1000'0000);

        if ((first & 0b1110'0000) == 0b1100'0000) {
            uint32_t code_point = ((first & 0b0001'1111) << 6) | (second & 0b0011'1111);
            return code_point;
        } else {
            assert(remaining >= 3);

            char third = *it++;
            assert((third & 0b1100'0000) == 0b1000'0000);

            if ((first & 0b1111'0000) == 0b1110'0000) {
                uint32_t code_point = ((first & 0b0000'1111) << 12) | ((second & 0b0011'1111) << 6) | (third & 0b0011'1111);
                return code_point;
            } else {
                assert((first & 0b1111'1000) == 0b1111'0000);
                assert(remaining >= 4);

                char fourth = *it++;
                assert((fourth & 0b1100'0000) == 0b1000'0000);

                uint32_t code_point = ((first & 0b0000'0111) << 18) | ((second & 0b0011'1111) << 12) | ((third & 0b0011'1111) << 6) | (fourth & 0b0011'1111);
                return code_point;
            }
        }
    }
}

static uint32_t get_code_point_from_surrogates(wchar_t leading, wchar_t trailing) {
    assert(trailing >= 0xDC00);
    assert(trailing <= 0xDFFF);
    assert(leading >= 0xD800);
    assert(leading <= 0xDBFF);

    uint32_t high_10_bits = leading - 0xD800;
    uint32_t low_10_bits = trailing - 0xDC00;
    uint32_t subtracted = low_10_bits + (high_10_bits << 10);
    uint32_t code_point = subtracted + 0x01'0000;
    return code_point;
}

static uint32_t get_code_point(std::span<const wchar_t> utf16, std::span<const wchar_t>::iterator &it) {
    auto remaining = utf16.end() - it;
    assert(remaining >= 1);

    wchar_t leading = *it++;
    if ((leading <= 0xD7FF) || ((leading >= 0xE000) && (leading <= 0xFFFF))) {
        return leading;
    } else {
        assert(remaining >= 2);
        wchar_t trailing = *it++;

        uint32_t code_point = get_code_point_from_surrogates(leading, trailing);
        return code_point;
    }
}

std::vector<uint32_t> get_code_points(std::span<const char> utf8) {
    size_t count = get_code_point_count(utf8);
    std::vector<uint32_t> code_points;
    code_points.reserve(count);

    auto it = utf8.begin();
    while (it != utf8.end()) {
        uint32_t code_point = get_code_point(utf8, it);
        code_points.push_back(code_point);
    }

    return code_points;
}

std::vector<uint32_t> get_code_points(std::span<const wchar_t> utf16) {
    size_t count = get_code_point_count(utf16);
    std::vector<uint32_t> code_points;
    code_points.reserve(count);

    auto it = utf16.begin();
    while (it != utf16.end()) {
        uint32_t code_point = get_code_point(utf16, it);
        code_points.push_back(code_point);
    }

    return code_points;
}

static size_t get_utf8_code_unit_count(uint32_t code_point) {
    if ((code_point >= 0x00) && (code_point <= 0x7F)) {
        return 1;
    } else if ((code_point >= 0x0080) && (code_point <= 0x07FF)) {
        return 2;
    } else if ((code_point >= 0x0800) && (code_point <= 0xFFFF)) {
        return 3;
    } else {
        assert((code_point >= 0x01'0000) && (code_point <= 0x10'FFFF));
        return 4;
    }
}

static size_t get_utf16_code_unit_count(uint32_t code_point) {
    if ((code_point <= 0xD7FF) || ((code_point >= 0xE000) && (code_point <= 0xFFFF))) {
        return 1;
    } else {
        assert((code_point >= 0x1'0000) && (code_point <= 0x10'FFFF));
        return 2;
    }
}

static size_t get_utf8_code_unit_count(const std::vector<uint32_t> &code_points) {
    size_t count = 0;
    for (uint32_t code_point : code_points) {
        count += get_utf8_code_unit_count(code_point);
    }

    return count;
}

static size_t get_utf16_code_unit_count(const std::vector<uint32_t> &code_points) {
    size_t count = 0;
    for (uint32_t code_point : code_points) {
        count += get_utf16_code_unit_count(code_point);
    }

    return count;
}

static void put_code_point(std::span<char> utf8, std::span<char>::iterator &it, uint32_t code_point) {
    size_t remaining = utf8.end() - it;
    assert(remaining >= 1);

    size_t code_unit_count = get_utf8_code_unit_count(code_point);
    if (code_unit_count == 1) {
        *it++ = (char)code_point;
    } else if (code_unit_count == 2) {
        assert(remaining >= 2);

        char first = 0b1100'0000 | ((code_point >> 6) & 0b0001'1111);
        char second = 0b1000'0000 | (code_point & 0b0011'1111);

        *it++ = first;
        *it++ = second;
    } else if (code_unit_count == 3) {
        assert(remaining >= 3);

        char first = 0b1110'0000 | ((code_point >> 12) & 0b0000'1111);
        char second = 0b1000'0000 | ((code_point >> 6) & 0b0011'1111);
        char third = 0b1000'0000 | (code_point & 0b0011'1111);

        *it++ = first;
        *it++ = second;
        *it++ = third;
    } else {
        assert(code_unit_count == 4);
        assert(remaining >= 4);

        char first = 0b1111'0000 | ((code_point >> 18) & 0b0000'0111);
        char second = 0b1000'0000 | ((code_point >> 12) & 0b0011'1111);
        char third = 0b1000'0000 | ((code_point >> 6) & 0b0011'1111);
        char fourth = 0b1000'0000 | (code_point & 0b0011'1111);

        *it++ = first;
        *it++ = second;
        *it++ = third;
        *it++ = fourth;
    }
}

static void put_code_point(std::span<wchar_t> &utf16, std::span<wchar_t>::iterator &it, uint32_t code_point) {
    size_t remaining = utf16.end() - it;
    assert(remaining >= 1);

    size_t code_unit_count = get_utf16_code_unit_count(code_point);
    if (code_unit_count == 1) {
        *it++ = (wchar_t)code_point;
    } else {
        assert(remaining >= 2);
        assert(code_unit_count == 2);

        uint32_t subtracted = code_point - 0x10000;

        uint32_t high_10_bits = (subtracted & 0x0F'FC00) >> 10;
        uint32_t low_10_bits = subtracted & 0x03FF;

        wchar_t leading = (wchar_t)(0xD800 + high_10_bits);
        wchar_t trailing = (wchar_t)(0xDC00 + low_10_bits);

        *it++ = leading;
        *it++ = trailing;
    }
}

static std::string to_utf8(const std::vector<uint32_t> &code_points) {
    size_t utf8_length = get_utf8_code_unit_count(code_points);
    std::string utf8(utf8_length, 0);
    std::span<char> utf8_span(utf8.begin(), utf8.end());

    auto it = utf8_span.begin();
    for (uint32_t code_point : code_points) {
        assert(it < utf8_span.end());
        put_code_point(utf8_span, it, code_point);
    }

    return utf8;
}

static std::wstring to_utf16(const std::vector<uint32_t> &code_points) {
    size_t utf16_length = get_utf16_code_unit_count(code_points);
    std::wstring utf16(utf16_length, 0);
    std::span<wchar_t> utf16_span(utf16.begin(), utf16.end());

    auto it = utf16_span.begin();
    for (uint32_t code_point : code_points) {
        assert(it < utf16_span.end());
        put_code_point(utf16_span, it, code_point);
    }

    return utf16;
}

template <typename T>
    requires(std::is_same_v<T, char> || std::is_same_v<T, wchar_t>)
std::span<const T> get_span_without_null_terminator(std::span<const T> span) {
    if (span.empty()) {
        return span;
    }

    auto it = std::ranges::find(span, 0);

    if (it == span.end()) {
        return span;
    }

    if (it == std::prev(span.end())) {
        std::span<const T> new_span = { span.begin(), std::prev(span.end()) };
        return new_span;
    } else {
        abort(); // Null character that isn't at the end.
    }
}

std::wstring convert(std::span<const char> utf8) {
    std::span<const char> utf8_string = get_span_without_null_terminator(utf8);
    std::vector<uint32_t> code_points = get_code_points(utf8_string);
    std::wstring utf16 = to_utf16(code_points);
    return utf16;
}

std::string convert(std::span<const wchar_t> utf16) {
    std::span<const wchar_t> utf16_string = get_span_without_null_terminator(utf16);
    std::vector<uint32_t> code_points = get_code_points(utf16_string);
    std::string utf8 = to_utf8(code_points);
    return utf8;
}

std::wstring convert(const char *utf8_string) {
    std::span<const char> string = get_span(utf8_string);
    std::vector<uint32_t> code_points = get_code_points(string);
    std::wstring utf16 = to_utf16(code_points);
    return utf16;
}

std::string convert(const wchar_t *utf16_string) {
    std::span<const wchar_t> string = get_span(utf16_string);
    std::vector<uint32_t> code_points = get_code_points(string);
    std::string utf8 = to_utf8(code_points);
    return utf8;
}

std::span<const char> get_span(const char *string) {
    size_t length = strlen(string);
    std::span<const char> span = { string, string + length };
    return span;
}

std::span<const wchar_t> get_span(const wchar_t *string) {
    size_t length = wcslen(string);
    std::span<const wchar_t> span = { string, string + length };
    return span;
}