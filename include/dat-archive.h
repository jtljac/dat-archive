#pragma once
#include <cinttypes>
#include <filesystem>
#include <fstream>
#include <string>
#include <map>
#include <vector>

namespace DatArchive {
    /** The signature used by datarchive files */
    constexpr char DATFILESIGNATURE[4] = {'\xB1', '\x44', '\x41', '\x54'};

    /** The version of the datarchive supported by this library */
    constexpr uint8_t DATFILEVERSION = 0x01;

    constexpr size_t CHUNKSIZE = 262144;

    /**
     * The compression methods available
     */
    enum class CompressionMethod : uint8_t {
        NONE,
        ZLIB
    };

    /**
     * Extra flags that may apply to the file
     */
    struct Flags {
        /** Whether to encrypt the file in the archive */
        bool encrypted;

        Flags() : encrypted(false) {}

        Flags(bool encrypted) : encrypted(encrypted) {}

        explicit Flags(uint8_t flagByte);

        explicit operator uint8_t() const;;
    };

    /**
     * Metadata about a file stored inside an archive
     */
    struct TableEntry {
        // Table Entry Records
        /** The name of the file in the archive */
        std::string name;
        /** The compression method used for the file */
        CompressionMethod compressionMethod = CompressionMethod::NONE;
        /** Extra flags that apply to the file */
        Flags fileFlags;
        /** The CRC32 checksum for the file in the archive */
        uint32_t crc32 = 0;
        /** The original size (prior to compression) of the file */
        uint64_t originalSize = 0;
        /** The offset from the beginning of the archive file at which the file begins */
        uint64_t dataStart = 0;
        /** The offset from the beginning of the archive file immediately following the final byte of the file */
        uint64_t dataEnd = 0;

        TableEntry() = default;

        /**
         *
         * @param name The name of the file in the archive, this can include forward slashes to denote a path
         * @param cMethod The compression method to use for the file in the archive
         * @param flags Extra flags that may apply to the file
         */
        TableEntry(std::string name, CompressionMethod cMethod, Flags flags) : name(std::move(name)), compressionMethod(cMethod),
                                                                               fileFlags(flags) {}

       /**
        * Get the size of the file inside the archive
        * @return the size of the file inside the archive
        */
        [[nodiscard]] uint64_t sizeInArchive() const;
    };

    class DatArchiveReader {
        // Operational data
        std::filesystem::path archivePath;
        std::ifstream archive;

        // Archive file metadata
        uint8_t archiveVersion{};
        uint64_t tableOffset{};
        std::map<std::string, TableEntry> entries;

        // Flags
        bool openFlag = false;
        bool badFlag = false;

        /**
         * Check the archive is valid
         * @param signature The signature of the archive being checked
         * @param version The version of the archive being checked
         * @return
         */
        static bool validateArchive(char* signature, uint8_t version);

        /**
         * Load the table of the archive
         * @return True if successful
         */
        bool loadTable();

        /**
         * Retrieve a file from the archive using it's entry
         * @param entry The entry for the file
         * @param buffer The buffer to write the file into
         * @param validateCrc Whether to validate the CRC or the file
         * @return The size of the file
         */
        uint64_t getFileFromEntry(const TableEntry& entry, char* buffer, bool validateCrc = true);

        /**
         * Extract an uncompressed file from the archive using it's entry
         * @param entry The entry for the file
         * @param buffer The buffer to write the file into
         * @param validateCrc Whether to validate the CRC or the file
         * @return The size of the file
         */
        uint64_t extractFile(const TableEntry& entry, char* buffer, bool validateCrc);

        /**
         * Extract a compressed file from the archive using it's entry
         * @param entry The entry for the file
         * @param buffer The buffer to write the file into
         * @param validateCrc Whether to validate the CRC or the file
         * @return The size of the file
         */
        uint64_t zlibExtractFile(const TableEntry& entry, char* buffer, bool validateCrc);

    public:
        DatArchiveReader(const std::filesystem::path& archiveFilePath);

        /**
         * Open an archive
         * @param archiveFilePath The path to the archive
         * @return true if successful
         */
        bool openArchive(const std::filesystem::path& archiveFilePath);

        /**
         * Close the archive
         * @return true if successful
         */
        bool closeArchive();;

        /**
         * Get the number of files in the archive
         * @return the number of files in the archive
         */
        size_t size() const;

        bool contains(const std::string& name) const;

        /**
         * Get a list of all the file names in the archive
         * @returna list of all the file names in the archive
         */
        std::vector<std::string> listFiles() const;

        /**
         * Get a specific file from the archive
         * @param name The name of the file
         * @return A byte vector that represents the file, empty if the file doesn't exist
         */
        std::vector<char> getFile(const std::string& name);

        /**
         * get a specific file from the archive
         * <br>
         * Warning, this function assumes that the buffer is large enough to wholly contain the file.
         * <br>
         * The size of the file can be obtained from it's file entry using getFileEntry()
         * @param name The name of the file
         * @param buffer The buffer to store the file in
         * @return The size of the file
         */
        uint64_t getFileRaw(const std::string& name, char* buffer);

//        /**
//         * Get a specific file from the archive and write it to the given stream
//         * @param name The name of the file
//         * @param stream The stream to write the file to
//         * @return true if successful
//         */
//        bool getFileToStream(const std::string& name, std::ostream& stream);

        /**
         * Get the file entry for the given filename
         * @param name The name of the file
         * @return The table entry that represents the file
         */
        const TableEntry& getFileEntry(const std::string& name) const;

        /**
         * Get the whole file table
         * @return The file table of the archive
         */
        std::vector<DatArchive::TableEntry> getTable() const;

        /**
         * Get the offset from the beginning of the file to the Entry Table
         * @return The offset of the Entry Table
         */
        uint64_t getTableOffset() const;

        /**
         * Check if the archive is currently open
         * @return True if the archive is open
         */
        bool isOpen() const;

        /**
         * Check if the archive has experienced an error
         * @return True if the archive has errored
         */
        bool isBad() const;
    };

    /**
     * A class for writing DatArchive Files
     */
    class DatArchiveWriter {
        std::map<std::filesystem::path, TableEntry> fileEntries;

    private:
        /**
         * Write the header of the archive
         * <br>
         * This assumes the stream pointer is at the beginning of the file
         * @param archiveFile The archive file to write to
         */
        static void writeHeader(std::fstream& archiveFile);

        /**
         * Write the queued files into the archive
         * <br>
         * This assumes the stream pointer immediately follows the header
         * @param archiveFile The archive file to write to
         */
        void writeFiles(std::fstream& archiveFile);

        /**
         * Write the given file to the archive
         * @param file The file to write into the archive
         * @param archiveFile The archive file to write to
         * @param entry The file entry of the file
         */
        static void writeFileToArchive(std::fstream& file, std::fstream& archiveFile, TableEntry& entry);

        /**
         * Compress the given file and write it to the archive
         * @param file The file to compress and write into the archive
         * @param archiveFile The archive file to write to
         * @param entry The file entry of the file
         * @return The ZLib return code for the compression operation
         */
        static int zlibCompressFileToArchive(std::fstream& file, std::fstream& archiveFile, TableEntry& entry);

        /**
         * Write the location of the table to the header
         * <br>
         * This assumes the stream pointer is at the location at which the header will be written
         * @param archiveFile
         */
        void writeTableLocation(std::fstream& archiveFile);

        /**
         * Write the Entry Table to the archive
         * <br>
         * This assumes the stream pointer is immediately after the data
         * @param archiveFile The archive file to write to
         */
        void writeTable(std::fstream& archiveFile);

        /**
         * Write the given Entry Table to the archive
         * <br>
         * This assumes the stream pointer is immediately after the data
         * @param archiveFile The archive file to write to
         * @param entries The entries to write to the archive
         */
        void writeTable(std::fstream& archiveFile, const std::vector<TableEntry>& entries);

        /**
         * Write the given Entry to the archive
         * <br>
         * This assumes the stream pointer is in the correct player already
         * @param archiveFile The archive file to write to
         * @param entry The entry to write
         */
        void writeTableEntry(std::fstream& archiveFile, const TableEntry& entry);

    public:
        /**
         * Queue a file to be inserted into the archive
         * @param path The path to the file that is being inserted
         * @param entry An entry representing the file being inserted
         * @return true if the queue succeeds, false if that file has already been queued
         */
        bool queueFile(const std::filesystem::path& path, TableEntry entry);

        /**
         * Remove a file that has been queued
         * @param path The path to the file that will be removed from the queue
         * @return true if the file was removed from the queue, false if the file was not in the queue
         */
        bool removeFile(const std::filesystem::path& path);

        /**
         * Remove all files from the queue
         */
        void clear();

        /**
         * Write the archive to the given destination
         * @param destination The destination to write the archive to
         * @param overwrite Whether to overwrite the file at the destination if it exists
         * @return true if successful
         */
        bool writeArchive(const std::filesystem::path& destination, bool overwrite = false);

        /**
         * Append the queued files to an existing archive
         * <br>
         * This can add new files to the archive, but will not remove or overwrite existing files, any files queued to
         * be added that share a name with a file already in the archive will be discarded.
         * @param destinationArchive The existing archive to append the new files onto
         * @return true if successful
         */
        bool appendArchive(const std::filesystem::path& destinationArchive);
    };
}