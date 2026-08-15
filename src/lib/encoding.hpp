#pragma once

std::span<const char> get_span(const char *string);
std::span<const wchar_t> get_span(const wchar_t *string);
std::vector<uint32_t> get_code_points(std::span<const char> utf8);
std::vector<uint32_t> get_code_points(std::span<const wchar_t> utf16);
std::string convert(const wchar_t *utf16_string);
std::string convert(std::span<const wchar_t> utf16);
std::wstring convert(const char *utf8_string);
std::wstring convert(std::span<const char> utf8);