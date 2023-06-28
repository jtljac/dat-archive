//
// Created by jacob on 27/06/23.
//

#include "../include/dat-archive.h"

#include <algorithm>
#include <bitset>
#include <iostream>
#include <zlib.h>
#include <cassert>
#include <cstring>

/*
 * Flags
 */

DatArchive::Flags::Flags(uint8_t flagByte) {
    std::bitset<8> bits(flagByte);

    encrypted = bits.test(0);
}

DatArchive::Flags::operator uint8_t() const {
    return 0b00000001 & encrypted;
}

/*
 * TableEntry
 */

uint64_t DatArchive::TableEntry::sizeInArchive() const {
    return dataEnd - dataStart;
}

/*
 * Reader
 */

bool DatArchive::DatArchiveReader::validateArchive(char* signature, uint8_t version) {
    return strncmp(DATFILESIGNATURE, signature, 4) == 0 && DATFILEVERSION == version;
}

bool DatArchive::DatArchiveReader::loadTable() {
    archive.seekg(tableOffset);

    if (archive.fail() || tableOffset == 0) {
        return false;
    }

    uint16_t nameLength;
    while (archive.read(reinterpret_cast<char*>(&nameLength), 2)) {
        TableEntry entry;

        // Name
        entry.name.resize(nameLength);
        archive.read(entry.name.data(), nameLength);

        // Compression Method
        archive.read(reinterpret_cast<char*>(&entry.compressionMethod), 1);

        // Flags
        archive.read(reinterpret_cast<char*>(&entry.fileFlags), 1);

        // crc32
        archive.read(reinterpret_cast<char*>(&entry.crc32), 4);

        // Original Size
        archive.read(reinterpret_cast<char*>(&entry.originalSize), 8);

        // Data Start
        archive.read(reinterpret_cast<char*>(&entry.dataStart), 8);

        // Data End
        archive.read(reinterpret_cast<char*>(&entry.dataEnd), 8);

        entries.emplace(entry.name, entry);
    }

    archive.clear();
    archive.seekg(0);

    return true;
}

uint64_t
DatArchive::DatArchiveReader::getFileFromEntry(const DatArchive::TableEntry& entry, char* buffer, bool validateCrc) {
    switch (entry.compressionMethod) {
        case CompressionMethod::NONE:
            return extractFile(entry, buffer, validateCrc);
            break;
        case CompressionMethod::ZLIB:
            return zlibExtractFile(entry, buffer, validateCrc);
            break;
    }

    return 0;
}

uint64_t DatArchive::DatArchiveReader::extractFile(const DatArchive::TableEntry& entry, char* buffer, bool validateCrc) {
    archive.seekg(entry.dataStart);
    archive.read(buffer, entry.sizeInArchive());

    uint32_t calculatedCrc = crc32(0, reinterpret_cast<unsigned char*>(buffer), entry.sizeInArchive());

    if (validateCrc && calculatedCrc != entry.crc32) return 0;

    return (uint64_t) archive.tellg() - entry.dataStart;
}

uint64_t
DatArchive::DatArchiveReader::zlibExtractFile(const DatArchive::TableEntry& entry, char* buffer, bool validateCrc) {
    archive.seekg(entry.dataStart);

    int rc;
    unsigned have;
    z_stream strm;

    unsigned char* in = new unsigned char[CHUNKSIZE];

    uint32_t calculatedCrc = 0;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    strm.avail_out = entry.originalSize;
    strm.next_out = reinterpret_cast<unsigned char*>(buffer);

    rc = inflateInit(&strm);
    if (rc != Z_OK) return 0;

    do {
        uint64_t availableBytes = (uint64_t) archive.tellg() + CHUNKSIZE < entry.dataEnd
                                  ? CHUNKSIZE
                                  : entry.dataEnd - (uint64_t) archive.tellg();
        archive.read(reinterpret_cast<char*>(in), availableBytes);
        strm.avail_in = availableBytes;
        strm.next_in = in;

        if (archive.bad()) {
            badFlag = true;
            inflateEnd(&strm);
            delete[] in;
            return 0;
        }

        calculatedCrc = crc32(calculatedCrc, in, availableBytes);

        rc = inflate(&strm, Z_NO_FLUSH);
        switch (rc) {
            case Z_NEED_DICT:
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                badFlag = false;
                inflateEnd(&strm);
                delete[] in;
                return 0;
        }
    } while (rc != Z_STREAM_END);

    inflateEnd(&strm);

    if (validateCrc && calculatedCrc != entry.crc32) return 0;

    return entry.originalSize;
}

DatArchive::DatArchiveReader::DatArchiveReader(const std::filesystem::path& archiveFilePath) : archivePath(archiveFilePath) {
    openArchive(archiveFilePath);
}

bool DatArchive::DatArchiveReader::openArchive(const std::filesystem::path& archiveFilePath) {
    openFlag = false;
    badFlag = false;

    if (!exists(archiveFilePath) && !is_directory(archiveFilePath)) {
        return false;
    }
    archive.open(archiveFilePath, std::ios::in | std::ios::binary);

    if (!archive) {
        return false;
    }
    openFlag = true;

    char signature[4];

    archive.read(signature, 4);
    archive.read(reinterpret_cast<char*>(&archiveVersion), 1);

    if (!validateArchive(signature, archiveVersion)) {
        badFlag = true;
        return false;
    }

    archive.read(reinterpret_cast<char*>(&tableOffset), 8);

    return loadTable();
}

bool DatArchive::DatArchiveReader::closeArchive() {
    if (openFlag) return false;
    archive.close();
    openFlag = false;
    return true;
}

size_t DatArchive::DatArchiveReader::size() const {
    return entries.size();
}

bool DatArchive::DatArchiveReader::contains(const std::string& name) const {
    return entries.count(name) > 0;
}

std::vector<std::string> DatArchive::DatArchiveReader::listFiles() const {
    std::vector<std::string> keys(entries.size());

    // Extract keys from entries
    std::transform(entries.begin(), entries.end(), keys.begin(), [keys](auto pair) {return pair.first;});

    return keys;
}

std::vector<char> DatArchive::DatArchiveReader::getFile(const std::string& name) {
    if (!openFlag || badFlag) return {};

    if (!contains(name)) return {};
    const TableEntry& entry = getFileEntry(name);

    std::vector<char> dest(entry.originalSize);

    if (getFileFromEntry(entry, dest.data())) return dest;
    else return {};
}

uint64_t DatArchive::DatArchiveReader::getFileRaw(const std::string& name, char* buffer) {
    if (!openFlag || badFlag) return 0;
    if (!contains(name)) return {};
    const TableEntry& entry = getFileEntry(name);

    return getFileFromEntry(entry, buffer);
}

const DatArchive::TableEntry& DatArchive::DatArchiveReader::getFileEntry(const std::string& name) const {
    if (!openFlag || badFlag) return {};
    return entries.at(name);
}

std::vector<DatArchive::TableEntry> DatArchive::DatArchiveReader::getTable() const {
    std::vector<TableEntry> table(entries.size());

    // Extract keys from entries
    std::transform(entries.begin(), entries.end(), table.begin(), [table](auto pair) {return pair.second;});

    return table;
}

uint64_t DatArchive::DatArchiveReader::getTableOffset() const {
    return tableOffset;
}

bool DatArchive::DatArchiveReader::isOpen() const {
    return openFlag;
}

bool DatArchive::DatArchiveReader::isBad() const {
    return badFlag;
}

/*
 * Writer
 */

void DatArchive::DatArchiveWriter::writeHeader(std::fstream& archiveFile) {
    archiveFile.write(DATFILESIGNATURE, 4);
    archiveFile.write(reinterpret_cast<const char*>(&DATFILEVERSION), 1);

    // Placeholder until we work out where the tableOffset will actually be
    char empty[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    archiveFile.write(empty, 8);

    // Write to file
    archiveFile.flush();
}

void DatArchive::DatArchiveWriter::writeFiles(std::fstream& archiveFile) {
    for (auto& [path, entry]: fileEntries) {
        // Open file
        std::fstream theFile(path, std::ios::binary | std::ios::in | std::ios::ate);

        if (theFile.fail()) {
            std::cout << "Failed to open \"" << path << "\", It has not been written to the archive file." << std::endl;
            continue;
        }

        entry.dataStart = archiveFile.tellp();
        entry.originalSize = theFile.tellg();
        theFile.seekg(0);

        switch (entry.compressionMethod) {
            case CompressionMethod::NONE:
                writeFileToArchive(theFile, archiveFile, entry);
                break;
            case CompressionMethod::ZLIB:
                zlibCompressFileToArchive(theFile, archiveFile, entry);
                break;
        }
        theFile.close();

        entry.dataEnd = archiveFile.tellp();
    }

    archiveFile.flush();
}

void DatArchive::DatArchiveWriter::writeFileToArchive(std::fstream& file, std::fstream& archiveFile,
                                                      DatArchive::TableEntry& entry) {
    // Amount left
    unsigned have;

    // CRC32
    entry.crc32 = crc32(0L, Z_NULL, 0);

    // Buffers
    unsigned char* buffer = new unsigned char[CHUNKSIZE];

    while (!file.eof()) {
        /*
         * We'd probably have enough memory for this anyway, but we'll chunk it anyway, just in case.
         */
        // Fill buffer
        file.read(reinterpret_cast<char*>(buffer), CHUNKSIZE);
        have = file.gcount();

        // Generate CRC
        entry.crc32 = crc32(entry.crc32, buffer, have);

        // Write to file
        archiveFile.write(reinterpret_cast<char*>(buffer), have);
    }
}

int DatArchive::DatArchiveWriter::zlibCompressFileToArchive(std::fstream& file, std::fstream& archiveFile,
                                                            DatArchive::TableEntry& entry) {
    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char* in = new unsigned char[CHUNKSIZE];
    unsigned char* out = new unsigned char[CHUNKSIZE];

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) return ret;

    /* compress until end of file */
    do {
        file.read(reinterpret_cast<char*>(in), CHUNKSIZE);
        strm.avail_in = file.gcount();

        // If there was a failure reading the file, cleanup and exit early
        if (file.bad()) {
            std::cerr << "Failed to read from input file during compression" << std::endl;
            deflateEnd(&strm);
            delete[] in;
            delete[] out;

            return Z_ERRNO;
        }

        flush = file.eof() ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
           compression if all the source has been read in */
        do {
            strm.avail_out = CHUNKSIZE;
            strm.next_out = out;
            ret = deflate(&strm, flush);

            // Handle if state gets clobbered
            if (ret == Z_STREAM_ERROR) {
                std::cerr << "Compression resulted in bad state" << std::endl;
                deflateEnd(&strm);
                delete[] in;
                delete[] out;

                return ret;
            }

            have = CHUNKSIZE - strm.avail_out;
            entry.crc32 = crc32(entry.crc32, out, have);

            uint32_t diff = archiveFile.tellp();
            archiveFile.write(reinterpret_cast<char*>(out), have);
            diff = ((uint32_t) archiveFile.tellp()) - diff;

            if (diff != have || archiveFile.fail()) {
                std::cerr << "Failed to write to archive file during compression" << std::endl;
                deflateEnd(&strm);
                delete[] in;
                delete[] out;

                return ret;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);

    deflateEnd(&strm);

    delete[] in;
    delete[] out;

    return Z_OK;
}

void DatArchive::DatArchiveWriter::writeTableLocation(std::fstream& archiveFile) {
    uint64_t tableOffset = archiveFile.tellp();
    // Write table offset
    archiveFile.seekp(5);
    archiveFile.write(reinterpret_cast<char*>(&tableOffset), 8);
    archiveFile.seekp(tableOffset);
}

void DatArchive::DatArchiveWriter::writeTable(std::fstream& archiveFile) {
    for (const auto& [path, entry]: fileEntries) {
        writeTableEntry(archiveFile, entry);
    }

    archiveFile.flush();
}

void DatArchive::DatArchiveWriter::writeTable(std::fstream& archiveFile, const std::vector<TableEntry>& entries) {
    for (const auto& entry: entries) {
        writeTableEntry(archiveFile, entry);
    }

    archiveFile.flush();
}

void DatArchive::DatArchiveWriter::writeTableEntry(std::fstream& archiveFile, const DatArchive::TableEntry& entry) {
    // Name
    uint16_t nameSize = entry.name.size();
    archiveFile.write(reinterpret_cast<char*>(&nameSize), 2);
    archiveFile.write(entry.name.data(), nameSize);

    // Compression method
    archiveFile.write(reinterpret_cast<const char*>(&entry.compressionMethod), 1);

    // flags
    uint8_t flagBuffer = (uint8_t) entry.fileFlags;
    archiveFile.write(reinterpret_cast<char*>(&flagBuffer), 1);

    // crc32
    archiveFile.write(reinterpret_cast<const char*>(&entry.crc32), 4);

    // Original Size
    archiveFile.write(reinterpret_cast<const char*>(&entry.originalSize), 8);

    // dataStart
    archiveFile.write(reinterpret_cast<const char*>(&entry.dataStart), 8);

    // dataEnd
    archiveFile.write(reinterpret_cast<const char*>(&entry.dataEnd), 8);
}

bool DatArchive::DatArchiveWriter::queueFile(const std::filesystem::path& path, DatArchive::TableEntry entry) {
    if (fileEntries.count(path) > 0) {
        std::cout << "File \"" << path << "\" has already been queued.";
        return false;
    } if (!exists(path)) {
        std::cout << "File \"" << path << "\" does not exist.";
        return false;
    }

    fileEntries.emplace(path, entry);

    return true;
}

bool DatArchive::DatArchiveWriter::removeFile(const std::filesystem::path& path) {
    return fileEntries.erase(path) > 0;
}

void DatArchive::DatArchiveWriter::clear() {
    fileEntries.clear();
}

bool DatArchive::DatArchiveWriter::writeArchive(const std::filesystem::path& destination, bool overwrite) {
    if (exists(destination)) {
        if (overwrite) {
            removeFile(destination);
        } else {
            std::cout << "File \"" << destination << "\" already exists.";
            return false;
        }
    }

    create_directories(destination.parent_path());
    std::fstream stream(destination, std::ios::binary | std::ios::out);

    writeHeader(stream);
    writeFiles(stream);
    writeTableLocation(stream);
    writeTable(stream);

    stream.flush();
    stream.close();

    return true;
}

bool DatArchive::DatArchiveWriter::appendArchive(const std::filesystem::path& destinationArchive) {
    if (!exists(destinationArchive)) {
        std::cout << "File \"" << destinationArchive << "\" does not exists.";
        return false;
    }

    // Read information from the archive
    DatArchiveReader archive(destinationArchive);

    if (archive.isBad() || !archive.isOpen()) {
        std::cout << "Failed to open archive file at \"" << destinationArchive << "\"";
        return false;
    }

    uint64_t tableOffset = archive.getTableOffset();
    std::vector<TableEntry> entries = archive.getTable();
    archive.closeArchive();

    // Sort the entries to maintain their order
    std::sort(entries.begin(), entries.end(), [](const TableEntry& a, const TableEntry& b) {return a.dataStart < b.dataStart;});

    // Filter queued files to skip any that are already in the archive
    auto it = fileEntries.begin();
    while (it != fileEntries.end()) {
        if (std::count_if(entries.begin(), entries.end(), [&it](const TableEntry& entry){return entry.name == it->second.name;})) {
            std::cout << "A file with the name \"" << it->second.name << "\" already exists in the archive, it will be skipped";
            fileEntries.erase(it++);
        } else ++it;
    }

    std::fstream stream(destinationArchive, std::ios::binary | std::ios::in | std::ios::out);
    stream.seekp(tableOffset);

    writeFiles(stream);
    writeTableLocation(stream);

    // Write old table then new table
    writeTable(stream, entries);
    writeTable(stream);

    stream.flush();
    stream.close();

    return true;
}
