//
// Created by jacob on 27/06/23.
//

#include <iostream>

#include <dat-archive.h>

int main() {
    DatArchive::DatArchiveWriter writer;

    writer.queueFile("/home/jacob/Downloads/zpipe.c", DatArchive::TableEntry("Test", DatArchive::CompressionMethod::ZLIB, DatArchive::Flags()));
    writer.queueFile("/home/jacob/Downloads/document.pdf", DatArchive::TableEntry("Test2", DatArchive::CompressionMethod::ZLIB, DatArchive::Flags()));
    writer.queueFile("/home/jacob/Downloads/ssh-key-2023-06-06.key.pub", DatArchive::TableEntry("Test3/testing", DatArchive::CompressionMethod::NONE, DatArchive::Flags()));

    writer.writeArchive("./dest.dat", true);

    writer.clear();

    writer.queueFile("/home/jacob/Downloads/module(1).json", DatArchive::TableEntry("Testing", DatArchive::CompressionMethod::ZLIB, DatArchive::Flags()));

    writer.appendArchive("./dest.dat");

    DatArchive::DatArchiveReader reader("./dest.dat");
    std::vector<char> test = reader.getFile("Test");
    std::cout << test.data() << "a" << std::endl;
}