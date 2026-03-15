#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <vector>

class ConnectionIdBuilder
{
public:
    // connection_single_column.txt を読み込んで int16 配列にする
    // NOTE: Mozcのこのファイルは先頭行がコメント/ヘッダのことがあるため skip_first_line を用意
    static std::vector<std::int16_t> readSingleColumnText(
        const std::filesystem::path &path,
        bool skip_first_line = true);

    // Kotlin ByteBuffer(=Big Endian) と同等の 2byte*N バイナリ形式で書き出す
    static void writeShortArrayAsBytesBE(
        const std::vector<std::int16_t> &v,
        const std::filesystem::path &out_path);

    // 2byte*N バイナリから読む（Big Endian）
    static std::vector<std::int16_t> readShortArrayFromBytesBE(
        const std::filesystem::path &path);

    static std::vector<std::int16_t> readShortArrayFromBytesBE(std::istream &is);

private:
    static void write_u16_be(std::ostream &os, std::uint16_t v);
    static bool read_u16_be(std::istream &is, std::uint16_t &v);
};
