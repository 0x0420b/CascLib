/*
* Copyright 2015 Gunnar Lilleaasen
*
* This file is part of CascLib.
*
* CascLib is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.

* CascLib is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with CascLib.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <unordered_map>

#include "../../Common.hpp"
#include "../../Exceptions.hpp"

#include "../../Parsers/Binary/Reference.hpp"
#include "../../IO/StreamAllocator.hpp"
#include "../../IO/Endian.hpp"

namespace Casc
{
    namespace Parsers
    {
        namespace Binary
        {
            /**
             * Maps file content MD5 hash to file key.
             */
            class Encoding
            {
            public:
                struct FileInfo
                {
                    Hex hash;
                    size_t size;
                    std::vector<Hex> keys;
                };

                struct EncodedFileInfo
                {
                    Hex key;
                    size_t size;
                    std::string params;
                };

                /**
                 * Find the file info for a file hash.
                 */
                FileInfo findFileInfo(Hex hash) const
                {
                    auto index = -1;
                    Hex checksum;

                    for (auto i = 0U; i < headersA.size(); ++i)
                    {
                        if (headersA[i].first <= hash)
                        {
                            index = headersA.size() - 1 - i;
                            checksum = headersA[i].second;
                            break;
                        }
                    }

                    if (index == -1)
                    {
                        throw Exceptions::HashDoesNotExistException(hash.string());
                    }

                    auto files = parseEntry(index, checksum);

                    for (auto it = files.begin(); it != files.end(); ++it)
                    {
                        if (it->hash == hash)
                        {
                            return *it;
                        }
                    }

                    throw Exceptions::HashDoesNotExistException(hash.string());
                }

                /**
                 * Find the encoding info for a file key.
                 */
                EncodedFileInfo findEncodedFileInfo(Hex key) const
                {
                    auto index = -1;
                    Hex checksum;

                    for (auto i = 0U; i < headersB.size(); ++i)
                    {
                        if (headersB[i].first <= key)
                        {
                            index = headersB.size() - 1 - i;
                            checksum = headersB[i].second;
                            break;
                        }
                    }

                    if (index == -1)
                    {
                        throw Exceptions::KeyDoesNotExistException(key.string());
                    }

                    auto files = parseEncodedEntry(index, checksum);

                    for (auto it = files.begin(); it != files.end(); ++it)
                    {
                        if (it->key == key)
                        {
                            return *it;
                        }
                    }

                    throw Exceptions::KeyDoesNotExistException(key.string());
                }

                /**
                 * Get file info for a range of files.
                 */
                std::vector<FileInfo> listFileInfo(uint32_t offset, uint32_t count) const
                {
                    std::vector<FileInfo> list;

                    while (list.size() < count)
                    {
                        auto remaining = count - list.size();

                        auto index = -1;
                        Hex checksum;

                        if (offset < headersA.size())
                        {
                            index = headersA.size() - 1 - offset;
                            checksum = headersA[offset].second;
                        }
                        else
                        {
                            break;
                        }

                        auto files = parseEntry(index, checksum);
                        auto n = remaining < files.size() ? remaining : files.size();

                        files.insert(list.end(), files.begin(), files.begin() + n);

                        offset += n;
                    }

                    return list;
                }

                 /**
                  * Get encoding info for a range of files.
                  */
                std::vector<EncodedFileInfo> listEncodedFileInfo(uint32_t offset, uint32_t count) const
                {
                    std::vector<EncodedFileInfo> list;

                    while (list.size() < count)
                    {
                        auto remaining = list.size() - count;

                        auto index = -1;
                        Hex checksum;

                        if (offset < headersB.size())
                        {
                            index = headersB.size() - 1 - offset;
                            checksum = headersB[offset].second;
                        }
                        else
                        {
                            break;
                        }

                        auto files = parseEncodedEntry(index, headersB[index].second);
                        auto count = remaining < files.size() ? remaining : files.size();

                        files.insert(list.end(), files.begin(), files.begin() + count);

                        offset += count;
                    }

                    return list;
                }

            private:
                // The file signature.
                static const uint16_t Signature = 0x4E45;

                // The header size of an encoding file.
                static const unsigned int HeaderSize = 22U;

                // The size of each chunk body (second block for each table).
                static const unsigned int EntrySize = 4096U;

                std::vector<std::pair<Hex, Hex>> headersA;
                std::vector<char> tableA;
                size_t hashSizeA;

                std::vector<std::pair<Hex, Hex>> headersB;
                std::vector<char> tableB;
                size_t hashSizeB;

                // The encoding profiles
                std::vector<std::string> profiles;

                /**
                * Reads data from a stream and puts it in a struct.
                */
                template <IO::EndianType Endian = IO::EndianType::Little, typename T>
                const T &read(std::shared_ptr<std::istream> stream, T &value) const
                {
                    char b[sizeof(T)];
                    stream->read(b, sizeof(T));

                    return value = IO::Endian::read<Endian, T>(b);
                }

                /**
                 * Parse an entry in the table.
                 */
                std::vector<FileInfo> parseEntry(uint32_t index, Hex checksum) const
                {
                    std::vector<FileInfo> files;

                    auto begin = tableA.begin() + EntrySize * index;
                    auto end = begin + EntrySize;

                    Hex actual(md5(begin, end));

                    if (actual != checksum)
                    {
                        throw Exceptions::InvalidHashException(Crypto::lookup3(checksum, 0), Crypto::lookup3(actual, 0), "");
                    }

                    for (auto it = begin; it < end;)
                    {
                        auto keyCount =
                            IO::Endian::read<IO::EndianType::Little, uint16_t>(it);
                        it += sizeof(keyCount);

                        if (keyCount == 0)
                            break;

                        auto fileSize =
                            IO::Endian::read<IO::EndianType::Big, uint32_t>(it);
                        it += sizeof(fileSize);

                        auto checksumIt = it;
                        it += hashSizeA;

                        std::vector<Hex> keys;

                        for (auto i = 0U; i < keyCount; ++i)
                        {
                            keys.emplace_back(it, it + hashSizeA);
                            it += hashSizeA;
                        }

                        files.emplace_back(FileInfo{ { checksumIt, checksumIt + hashSizeA }, fileSize, keys });
                    }

                    return files;
                }

                /**
                 * Parse an entry in the table.
                 */
                std::vector<EncodedFileInfo> parseEncodedEntry(uint32_t index, Hex checksum) const
                {
                    std::vector<EncodedFileInfo> files;

                    auto begin = tableB.begin() + EntrySize * index;
                    auto end = begin + EntrySize;

                    Hex actual(md5(begin, end));

                    if (actual != checksum)
                    {
                        throw Exceptions::InvalidHashException(Crypto::lookup3(checksum, 0), Crypto::lookup3(actual, 0), "");
                    }

                    for (auto it = begin; it < end;)
                    {
                        auto checksumIt = it;
                        it += hashSizeB;

                        auto profileIndex =
                            IO::Endian::read<IO::EndianType::Big, int32_t>(it);
                        it += sizeof(profileIndex);

                        ++it;

                        auto fileSize =
                            IO::Endian::read<IO::EndianType::Big, uint32_t>(it);
                        it += sizeof(fileSize);

                        auto &profile = profiles[profileIndex];

                        if (profileIndex >= 0)
                        {
                            files.emplace_back(EncodedFileInfo{ { checksumIt, checksumIt + hashSizeB }, fileSize, profile });
                        }
                        else
                        {
                            files.emplace_back(EncodedFileInfo{ { checksumIt, checksumIt + hashSizeB }, fileSize, "" });
                        }
                    }

                    return files;
                }

                /**
                * Parse an encoding file.
                */
                void parse(std::shared_ptr<std::istream> stream)
                {
                    uint16_t signature;
                    read<IO::EndianType::Little>(stream, signature);

                    if (signature != Signature)
                    {
                        throw Exceptions::InvalidSignatureException(signature, 0x4E45);
                    }

                    // Header

                    stream->seekg(1, std::ios_base::cur); // Skip unknown

                    uint8_t hashSizeA;
                    this->hashSizeA = read<IO::EndianType::Little, uint8_t>(stream, hashSizeA);

                    uint8_t hashSizeB;
                    this->hashSizeB = read<IO::EndianType::Little, uint8_t>(stream, hashSizeB);

                    stream->seekg(4, std::ios_base::cur); // Skip flags

                    uint32_t tableSizeA;
                    read<IO::EndianType::Big>(stream, tableSizeA);

                    uint32_t tableSizeB;
                    read<IO::EndianType::Big>(stream, tableSizeB);

                    stream->seekg(1, std::ios_base::cur); // Skip unknown

                    // Encoding profiles for table B

                    uint32_t stringTableSize;
                    read<IO::EndianType::Big>(stream, stringTableSize);

                    int ij = 0;
                    while (stream->tellg() < (HeaderSize + stringTableSize - 1))
                    {
                        std::string profile;
                        std::getline(*stream, profile, '\0');

                        if (stream->fail())
                        {
                            throw Exceptions::IOException("Stream faulted after getline()");
                        }

                        profiles.emplace_back(profile);
                        ij++;
                    }

                    // Table A
                    for (auto i = 0U; i < tableSizeA; ++i)
                    {
                        std::vector<char> hash(hashSizeA);
                        std::vector<char> checksum(hashSizeA);

                        stream->read(hash.data(), hashSizeA);
                        stream->read(checksum.data(), hashSizeA);

                        headersA.emplace_back(std::make_pair(hash, checksum));
                    }

                    std::reverse(headersA.begin(), headersA.end());

                    tableA.resize(EntrySize * tableSizeA);
                    stream->read(tableA.data(), tableA.size());

                    // Table B

                    for (auto i = 0U; i < tableSizeB; ++i)
                    {
                        std::vector<char> hash(hashSizeA);
                        std::vector<char> checksum(hashSizeA);

                        stream->read(hash.data(), hashSizeA);
                        stream->read(checksum.data(), hashSizeA);

                        headersB.emplace_back(std::make_pair(hash, checksum));
                    }

                    std::reverse(headersB.begin(), headersB.end());

                    tableB.resize(EntrySize * tableSizeB);
                    stream->read(tableB.data(), tableB.size());

                    // Encoding profile for this file

                    std::string profile;
                    std::getline(*stream, profile, '\0');

                    profiles.emplace_back(profile);
                }

            public:
                /**
                 * Constructor.
                 */
                Encoding(Parsers::Binary::Reference ref,
                         std::shared_ptr<IO::StreamAllocator> allocator)
                {
                    // Get a file stream.
                     auto fs = allocator->data<true, false>(ref.file());

                    // Read size.
                    fs->seekg(ref.offset() + 16, std::ios_base::beg);
                    std::array<char, sizeof(uint32_t)> arr;
                    fs->read(arr.data(), arr.size());
                    auto size = IO::Endian::read<IO::EndianType::Little, uint32_t>(arr.begin());

                    // Read params.
                    std::string params;
                    std::vector<char> buf(size - 20);
                    fs->read(buf.data(), buf.size());
                    for (int i = size - 21; i >= 2; --i)
                    {
                        if (buf[i] == '\xDA' && buf[i - 1] == '\x78')
                        {
                            ZStreamBase::char_t* out = nullptr;
                            size_t outSize = 0;

                            auto inSize = size - 20 - i + 1;
                            auto in = std::make_unique<ZStreamBase::char_t[]>(inSize);
                            ZInflateStream(reinterpret_cast<ZStreamBase::char_t*>(
                                buf.data() + i - 1), inSize).readAll(&out, outSize);
                            params.assign(reinterpret_cast<char*>(out), outSize);
                            break;
                        }
                    }

                    // Parse CASC stream.
                    parse(allocator->data(ref));
                }

                /**
                * Copy constructor.
                */
                Encoding(const Encoding &) = default;

                /**
                 * Move constructor.
                 */
                Encoding(Encoding &&) = default;

                /**
                * Copy operator.
                */
                Encoding &operator= (const Encoding &) = default;

                /**
                 * Move operator.
                 */
                Encoding &operator= (Encoding &&) = default;

                /**
                 * Destructor.
                 */
                virtual ~Encoding() = default;
            };
        }
    }
}
