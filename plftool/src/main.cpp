#include <iostream>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <fstream>
#include "plf_crc_table.hpp"

#pragma pack(push, 1)
struct PLFHeader {
  uint32_t magic;
  uint32_t hdr_version;
  uint32_t header_size;
  uint32_t entry_header_size;
  uint32_t unk_10;
  uint32_t unk_14;
  uint32_t unk_18;
  uint32_t unk_1C;
  uint32_t header_crc_seed;
  uint32_t version_major;
  uint32_t version_minor;
  uint32_t version_bugfix;
  uint32_t unk_30;
  uint32_t file_size;

  static PLFHeader parse(const std::byte *buf)
  {
    PLFHeader h;
    std::memcpy(&h, buf, 56);
    return h;
  }
};
#pragma pack(pop)

struct PLFEntryHeader {
  uint32_t type;
  uint32_t size;
  uint32_t crc;
  uint32_t loadaddr;
  uint32_t usize;

  static PLFEntryHeader parse(const std::byte *buf)
  {
    PLFEntryHeader h;
    std::memcpy(&h, buf, 20);
    return h;
  }
};

struct PLFEntry {
  int index;
  PLFEntryHeader h;
  uint32_t offset;
  uint32_t data_offset;
  std::vector<std::byte> data;
};

static std::vector<std::byte> read_file(const std::string &path)
{
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::cerr << "err: cannot open file '" << path << "'\n";
    std::exit(1);
  }

  std::vector<char> tmp((std::istreambuf_iterator<char>(f)),
      std::istreambuf_iterator<char>());

  std::vector<std::byte> out(tmp.size());
  if (!tmp.empty())
    std::memcpy(out.data(), tmp.data(), tmp.size());

  return out;
}

static void write_file(const std::string &path, const std::vector<std::byte> &buf)
{
  std::ofstream f(p, std::ios::binary);
  if (!f) {
    std::cerr << "err: cannot open file '" << path << "'\n";
    exit(1);
  }

  if (!buf.empty()) { 
    f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    if (!f) {
      std::cerr << "err: failed to write file '" << path << "'\n";
      exit(1);
    }
  }
}

static void
crc_update(uint32_t &crc, uint32_t &count, const std::vector<std::byte> &data)
{
  for (std::byte b : data) {
    uint8_t v = static_cast<uint8_t>(b);
    uint32_t idx = (v ^ (crc >> 24)) & 0xFF;

    crc = (plf_crc_table[idx] ^ ((crc << 8) & 0xFFFFFFFF)) & 0xFFFFFFFF;
  }

  count = (count + static_cast<uint32_t>(data.size())) & 0xFFFFFFFF;
}

static uint32_t crc_finalize(uint32_t crc, uint32_t count)
{
  while (count) {
    uint32_t idx = (count ^ (crc >> 24)) & 0xFF;
    crc = (plf_crc_table[idx] ^ ((crc << 8) & 0xFFFFFFFF))
      & 0xFFFFFFFF;
    count >>= 8;
  }

  return (~crc) & 0xFFFFFFFF;
}

static std::pair<PLFHeader, std::vector<PLFEntry>>
parse_plf(const std::vector<std::byte> &buf)
{
  if (buf.size() < 56) {
    std::cerr << "err: file too small\n";
    std::exit(1);
  }

  PLFHeader hdr = PLFHeader::parse(buf.data());

  if (hdr.magic != 0x21464C50) {
    std::cerr << "err: magic\n";
    std::exit(1);
  }

  if (hdr.file_size != buf.size()) {
    std::cerr << "err: size\n";
    std::exit(1);
  }

  if (hdr.entry_header_size != 0x14) {
    std::cerr << "err: entry_header_size\n";
    std::exit(1);
  }

  std::vector<PLFEntry> entries;
  uint32_t offset = hdr.header_size;
  int i = 0;

  while (offset < hdr.file_size) {
    if (offset + hdr.entry_header_size > hdr.file_size) {
      std::cerr << "err: out of bounds\n";
      std::exit(1);
    }

    const std::byte *ehp = buf.data() + offset;
    PLFEntryHeader eh = PLFEntryHeader::parse(ehp);

    uint32_t data_off = offset + hdr.entry_header_size;
    if (data_off + eh.size > hdr.file_size) {
      std::cerr << "err: out of bounds\n";
      std::exit(1);
    }

    PLFEntry e;
    e.index = i;
    e.h = eh;
    e.offset = offset;
    e.data_offset = data_off;

    e.data.assign(buf.begin() + data_off, buf.begin() + data_off + eh.size);

    entries.push_back(e);

    uint32_t total = hdr.entry_header_size + eh.size;
    offset = (offset + total + 3) & ~3;
    i++;
  }

  return { hdr, entries };
}

static void plf_info(const std::filesystem::path &p)
{
  auto data = read_file(p);
  std::cout << data.size() << "\n";
}

static void print_usage(const std::string &prog) {
  std::cerr
    << "usage: " << prog << " info plf_file\n";
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    print_usage(std::string(argv[0]));
    return 1;
  }

  std::string prog = argv[0];
  std::string cmd  = argv[1];

  if (cmd == "info") {
    if (argc != 3) {
      print_usage(prog);
      return 1;
    }
    plf_info(argv[2]);
  }

  return 0;
}


