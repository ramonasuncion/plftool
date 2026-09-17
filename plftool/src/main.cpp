#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <zlib.h>
#include "json.hpp"
#include "plf_crc_table.hpp"

using json = nlohmann::json;

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

  static PLFHeader
  parse(const std::byte *buf)
  {
    PLFHeader h;
    std::memcpy(&h, buf, 56);
    return h;
  }

  std::vector<std::byte>
  pack() const
  {
    std::vector<std::byte> v(56);
    std::memcpy(v.data(), this, 56);
    return v;
  }
};
#pragma pack(pop)

#pragma pack(push, 1)
struct PLFEntryHeader {
  uint32_t type;
  uint32_t size;
  uint32_t crc;
  uint32_t extra;
  uint32_t usize;

  static PLFEntryHeader
  parse(const std::byte *buf)
  {
    PLFEntryHeader h;
    std::memcpy(&h, buf, 20);
    return h;
  }

  std::vector<std::byte>
  pack() const
  {
    std::vector<std::byte> v(20);
    std::memcpy(v.data(), this, 20);
    return v;
  }
};
#pragma pack(pop)

struct PLFEntry {
  int index;
  PLFEntryHeader h;
  uint32_t offset;
  uint32_t data_offset;
  std::vector<std::byte> data;
};

static void
print_usage(const std::string &prog)
{
  std::cerr << "usage: " << prog << " info plf_file\n";
  std::cerr << "       " << prog << " unpack plf_file outdir\n";
  std::cerr << "       " << prog << " pack [--original] manifest out_plf\n";
  std::cerr << "\n";
  std::cerr << "Commands:\n";
  std::cerr << "  info    Show PLF file information\n";
  std::cerr << "  unpack  Extract firmware to outdir/ (creates entry_N/ subdirectories)\n";
  std::cerr << "  pack    Create firmware from manifest\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "  --original  Use original compressed data (bit-identical output)\n";
  std::cerr << "              Default: rebuild from extracted_rootfs/ (allows modification)\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  " << prog << " unpack firmware.plf out/\n";
  std::cerr << "  " << prog << " pack out/manifest.json firmware_new.plf  # rebuild (modifiable)\n";
  std::cerr << "  " << prog << " pack --original out/manifest.json firmware_new.plf  # bit-identical\n";
}

static std::vector<std::byte>
read_file(const std::string &path)
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

static void
write_file(const std::string &path,
    const std::vector<std::byte> &buf)
{
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    std::cerr << "err: cannot open file for writing '" << path << "'\n";
    std::exit(1);
  }

  if (!buf.empty()) {
    f.write(reinterpret_cast<const char *>(buf.data()), buf.size());
    if (!f) {
      std::cerr << "err: failed while writing '" << path << "'\n";
      std::exit(1);
    }
  }
}

static json
read_json(const std::string &path)
{
  std::ifstream f(path);
  if (!f) {
    std::cerr << "err: cannot open JSON file '" << path << "'\n";
    std::exit(1);
  }

  try {
    json j;
    f >> j;
    return j;
  } catch (const std::exception &e) {
    std::cerr << "err: JSON parse error in '" << path
      << "': " << e.what() << "\n";
    std::exit(1);
  }

  std::exit(1);
}

static void
crc_update(uint32_t &crc,
    uint32_t &count,
    const std::vector<std::byte> &data)
{
  for (std::byte b : data) {
    uint8_t v = static_cast<uint8_t>(b);
    uint32_t idx = (v ^ (crc >> 24)) & 0xFF;
    crc = (plf_crc_table[idx] ^ ((crc << 8) & 0xFFFFFFFF))
      & 0xFFFFFFFF;
  }

  count = (count + static_cast<uint32_t>(data.size())) & 0xFFFFFFFF;
}

static uint32_t
crc_finalize(uint32_t crc, uint32_t count)
{
  // The count gets mixed in byte by byte so the CRC matches the reference tool
  uint32_t value = count;

  if (value == 0) {
    return (~crc) & 0xFFFFFFFF;
  }

  do {
    uint8_t tbIdx = ((crc >> 24) ^ value) & 0xFF;
    crc = (plf_crc_table[tbIdx] ^ (crc << 8)) & 0xFFFFFFFF;
    value >>= 8;
  } while (value != 0);

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
    std::cerr << "err: bad magic\n";
    std::exit(1);
  }

  if (hdr.file_size != buf.size()) {
    std::cerr << "err: size mismatch\n";
    std::exit(1);
  }

  if (hdr.entry_header_size != 0x14) {
    std::cerr << "err: bad entry_header_size\n";
    std::exit(1);
  }

  std::vector<PLFEntry> entries;
  uint32_t offset = hdr.header_size;
  int idx = 0;

  while (offset < hdr.file_size) {
    if (offset + hdr.entry_header_size > hdr.file_size) {
      std::cerr << "err: entry header out of bounds\n";
      std::exit(1);
    }

    const std::byte *ehp = buf.data() + offset;
    PLFEntryHeader eh = PLFEntryHeader::parse(ehp);

    uint32_t data_off = offset + hdr.entry_header_size;
    if (data_off + eh.size > hdr.file_size) {
      std::cerr << "err: entry data out of bounds\n";
      std::exit(1);
    }

    PLFEntry e;
    e.index = idx;
    e.h = eh;
    e.offset = offset;
    e.data_offset = data_off;

    e.data.assign(buf.begin() + data_off,
        buf.begin() + data_off + eh.size);

    entries.push_back(e);

    uint32_t total = hdr.entry_header_size + eh.size;
    offset = (offset + total + 3) & ~3;
    idx++;
  }

  return {hdr, entries};
}

static uint32_t
recompute_header_crc(const std::vector<std::byte> &buf,
    const PLFHeader &hdr)
{
  uint32_t crc = 0;
  uint32_t count = 0;

  if (hdr.header_size > buf.size()) {
    std::cerr << "err: header_size too large\n";
    std::exit(1);
  }

  std::vector<std::byte> head(buf.begin(),
      buf.begin() + hdr.header_size);

  uint32_t zero = 0;
  if (hdr.header_size >= 0x24)
    std::memcpy(head.data() + 0x20, &zero, 4);

  crc_update(crc, count, head);

  uint32_t offset = hdr.header_size;
  bool seen_last = false;
  uint32_t saved_crc = 0;
  uint32_t saved_count = 0;

  while (offset < hdr.file_size) {
    if (offset + hdr.entry_header_size > hdr.file_size) {
      std::cerr << "err: entry header out of bounds "
        "while recomputing CRC\n";
      std::exit(1);
    }

    const std::byte *ehp = buf.data() + offset;
    PLFEntryHeader eh = PLFEntryHeader::parse(ehp);

    if (!seen_last) {
      saved_crc = crc;
      saved_count = count;
    }

    std::vector<std::byte> chunk(buf.begin() + offset,
        buf.begin() + offset +
        hdr.entry_header_size);

    crc_update(crc, count, chunk);

    uint32_t total = hdr.entry_header_size + eh.size;
    offset = (offset + total + 3) & ~3;

    if (eh.type == 0x0D)
      seen_last = true;
  }

  if (seen_last) {
    crc = saved_crc;
    count = saved_count;
  }

  return crc_finalize(crc, count);
}

static std::string
hex2(uint32_t v)
{
  std::ostringstream oss;
  oss << std::uppercase << std::hex
    << std::setw(2) << std::setfill('0')
    << v;
  return oss.str();
}

static bool
is_gzip(const std::vector<std::byte> &data)
{
  if (data.size() < 3)
    return false;

  unsigned char b0 = static_cast<unsigned char>(data[0]);
  unsigned char b1 = static_cast<unsigned char>(data[1]);
  unsigned char b2 = static_cast<unsigned char>(data[2]);

  return b0 == 0x1f && b1 == 0x8b && b2 == 0x08;
}

static std::vector<std::byte>
gzip_decompress(const std::vector<std::byte> &in, uint32_t expected_size)
{
  if (in.empty()) {
    if (expected_size != 0) {
      std::cerr << "err: empty input\n";
      std::exit(1);
    }
    return {};
  }

  std::vector<std::byte> out(expected_size);

  z_stream strm;
  std::memset(&strm, 0, sizeof(strm));

  strm.next_in = reinterpret_cast<Bytef *>(
      const_cast<std::byte *>(in.data()));
  strm.avail_in = static_cast<uInt>(in.size());
  strm.next_out = reinterpret_cast<Bytef *>(out.data());
  strm.avail_out = static_cast<uInt>(out.size());

  int ret = inflateInit2(&strm, 16 + MAX_WBITS);
  if (ret != Z_OK) {
    std::cerr << "err: gzip_decompress: inflateInit2 failed: " << ret << "\n";
    std::exit(1);
  }

  ret = inflate(&strm, Z_FINISH);
  if (ret != Z_STREAM_END) {
    std::cerr << "err: gzip_decompress: inflate failed, ret=" << ret << "\n";
    inflateEnd(&strm);
    std::exit(1);
  }

  if (strm.total_out != expected_size) {
    std::cerr << "err: gzip_decompress: total_out=" << strm.total_out
      << " expected=" << expected_size << "\n";
    inflateEnd(&strm);
    std::exit(1);
  }

  inflateEnd(&strm);
  return out;
}

static std::vector<std::byte>
gzip_compress(const std::vector<std::byte> &in)
{
  if (in.empty())
    return {};

  std::vector<std::byte> out;
  out.resize(in.size() + in.size() / 10 + 64 + 18);

  z_stream strm;
  std::memset(&strm, 0, sizeof(strm));

  strm.next_in = reinterpret_cast<Bytef *>(
      const_cast<std::byte *>(in.data()));
  strm.avail_in = static_cast<uInt>(in.size());
  strm.next_out = reinterpret_cast<Bytef *>(out.data());
  strm.avail_out = static_cast<uInt>(out.size());

  int ret = deflateInit2(&strm,
      Z_DEFAULT_COMPRESSION,
      Z_DEFLATED,
      15 + 16,
      8,
      Z_DEFAULT_STRATEGY);
  if (ret != Z_OK) {
    std::cerr << "err: deflateInit2 failed: " << ret << "\n";
    std::exit(1);
  }

  while (true) {
    ret = deflate(&strm, Z_FINISH);

    if (ret == Z_STREAM_END)
      break;

    if (ret != Z_OK && ret != Z_BUF_ERROR) {
      std::cerr << "err: deflate failed: " << ret << "\n";
      deflateEnd(&strm);
      std::exit(1);
    }

    if (strm.avail_out == 0) {
      size_t old_size = out.size();
      out.resize(old_size * 2);
      strm.next_out = reinterpret_cast<Bytef *>(out.data() + old_size);
      strm.avail_out = static_cast<uInt>(old_size);
    }
  }

  deflateEnd(&strm);
  out.resize(strm.total_out);
  return out;
}


static void
cmd_info(const std::string &path)
{
  std::vector<std::byte> data = read_file(path);
  auto parsed = parse_plf(data);
  PLFHeader hdr = parsed.first;
  const std::vector<PLFEntry> &entries = parsed.second;

  std::cout << "PLF file: " << path << "\n";
  std::cout << "  Size:            " << data.size() << "\n";
  std::cout << "  Magic:           0x"
    << std::hex << std::setw(8) << std::setfill('0')
    << hdr.magic << std::dec << "\n";
  std::cout << "  Header version:  " << hdr.hdr_version << "\n";
  std::cout << "  Header size:     " << hdr.header_size << "\n";
  std::cout << "  Entry hdr size:  " << hdr.entry_header_size << "\n";
  std::cout << "  Version:         "
    << hdr.version_major << "."
    << hdr.version_minor << "."
    << hdr.version_bugfix << "\n";
  std::cout << "  File size word:  " << hdr.file_size << "\n";
  std::cout << "  Sections:        " << entries.size() << "\n\n";

  for (const PLFEntry &e : entries) {
    std::cout << "  ["
      << std::setw(2) << std::setfill('0') << e.index
      << "] off=0x"
      << std::hex << std::setw(8) << std::setfill('0')
      << e.offset << std::dec
      << " type=0x" << hex2(e.h.type)
      << " size=" << e.h.size
      << " crc=0x"
      << std::hex << std::setw(8)
      << std::setfill('0') << e.h.crc
      << std::dec
      << " extra=0x"
      << std::hex << std::setw(8)
      << std::setfill('0') << e.h.extra
      << std::dec
      << " usize=" << e.h.usize
      << "\n";
  }

  uint32_t crc = recompute_header_crc(data, hdr);

  std::cout << "\n  header_crc_seed: 0x"
    << std::hex << std::setw(8)
    << std::setfill('0') << hdr.header_crc_seed
    << "\n";
  std::cout << "  recomputed CRC : 0x"
    << std::hex << std::setw(8)
    << std::setfill('0') << crc
    << "\n";
  std::cout << "  CRC match       : "
    << std::dec << (crc == hdr.header_crc_seed)
    << "\n";
}


static void
recursive_extract_fs(const std::filesystem::path &file_path,
      const std::filesystem::path &base_outdir, int depth = 0)
{
  if (depth > 8) return;
  if (!std::filesystem::is_regular_file(file_path)) return;
  std::vector<std::byte> file_data = read_file(file_path.string());
  if (file_data.size() < 4) return;
  uint32_t magic = 0;
  std::memcpy(&magic, file_data.data(), 4);
  std::string fs_type;
  std::string cmd;
  std::filesystem::path fs_dir;
  bool found = false;
  if (magic == 0x28cd3d45) {
    fs_type = "cramfs";
    found = true;
  } else if (magic == 0x73717368) {
    fs_type = "squashfs";
    found = true;
  } else if (file_data.size() > 262 && std::memcmp(file_data.data(), "070701", 6) == 0) {
    fs_type = "cpio";
    found = true;
  } else if (file_data.size() > 512 && std::memcmp(file_data.data() + 257, "ustar", 5) == 0) {
    fs_type = "tar";
    found = true;
  }
  if (found) {
    fs_dir = file_path.string() + ".fs_extracted";
    std::filesystem::create_directories(fs_dir);
    if (fs_type == "cramfs") {
      cmd = "cramfsck -x '" + fs_dir.string() + "' '" + file_path.string() + "'";
    } else if (fs_type == "squashfs") {
      cmd = "unsquashfs -d '" + fs_dir.string() + "' '" + file_path.string() + "'";
    } else if (fs_type == "cpio") {
      cmd = "cd '" + fs_dir.string() + "' && cpio -idmv < '" + file_path.string() + "'";
    } else if (fs_type == "tar") {
      cmd = "tar -xf '" + file_path.string() + "' -C '" + fs_dir.string() + "'";
    }
    std::cout << std::string(depth*2, ' ') << "[recursive] Extracting embedded " << fs_type << ": " << file_path << " -> " << fs_dir << std::endl;
    int ret = std::system(cmd.c_str());
    if (ret == 0) {
      for (auto &p : std::filesystem::recursive_directory_iterator(fs_dir)) {
        if (std::filesystem::is_regular_file(p)) {
          recursive_extract_fs(p.path(), base_outdir, depth+1);
        }
      }
    } else {
      std::cerr << "[recursive] Failed to extract embedded " << fs_type << " from " << file_path << std::endl;
    }
  }
}

static void
extract_type09_entry(const std::vector<std::byte>& dec,
    const std::filesystem::path& extracted_root, int entry_index)
{
  if (dec.empty()) return;

  std::filesystem::path entry_root = extracted_root / ("entry_" + std::to_string(entry_index));

  size_t name_end = 0;
  while (name_end < dec.size() && static_cast<char>(dec[name_end]) != '\0') ++name_end;
  if (name_end == 0 || name_end >= dec.size()) {
    std::cerr << "[type-09] Warning: empty or invalid entry name\n";
    return;
  }

  std::string entry_name(reinterpret_cast<const char*>(&dec[0]), name_end);
  size_t pos = name_end + 1;

  if (pos + 4 > dec.size()) {
    std::cerr << "[type-09] Warning: incomplete entry metadata\n";
    return;
  }

  uint32_t entry_tag = 0;
  std::memcpy(&entry_tag, &dec[pos], 4);
  uint32_t entry_flags = entry_tag;
  uint32_t entry_filetype = entry_flags >> 12;

  std::filesystem::path out_path = entry_root / entry_name;

  auto entry_perms = static_cast<std::filesystem::perms>(entry_flags & 07777);

  if (entry_filetype == 0x4) {
    std::filesystem::create_directories(out_path);
    std::filesystem::permissions(out_path, entry_perms);
    std::cout << "[type-09] Entry " << entry_index << " Directory: " << out_path << std::endl;
  } else if (entry_filetype == 0x8) {
    size_t file_data_start = pos + 12;
    if (file_data_start > dec.size()) {
      std::cerr << "[type-09] Warning: file data out of bounds for " << entry_name << "\n";
      return;
    }

    std::filesystem::create_directories(out_path.parent_path());

    std::vector<std::byte> file_data(dec.begin() + file_data_start, dec.end());
    write_file(out_path.string(), file_data);
    std::filesystem::permissions(out_path, entry_perms);
    std::cout << "[type-09] Entry " << entry_index << " File: " << out_path << " (" << file_data.size() << " bytes)" << std::endl;
  } else if (entry_filetype == 0xA) {
    size_t target_start = pos + 12;
    if (target_start >= dec.size()) {
      std::cerr << "[type-09] Warning: symlink target out of bounds for " << entry_name << "\n";
      return;
    }

    size_t target_end = target_start;
    while (target_end < dec.size() && static_cast<char>(dec[target_end]) != '\0') ++target_end;

    if (target_end == target_start) {
      std::cerr << "[type-09] Warning: empty symlink target for " << entry_name << "\n";
      return;
    }

    std::string target_name(reinterpret_cast<const char*>(&dec[target_start]), target_end - target_start);
    std::filesystem::create_directories(out_path.parent_path());

    std::error_code ec;
    std::filesystem::remove(out_path, ec);
    std::filesystem::create_symlink(target_name, out_path, ec);
    std::cout << "[type-09] Entry " << entry_index << " Symlink: " << out_path << " -> " << target_name << std::endl;
  } else {
    std::cerr << "[type-09] Warning: unknown entry type 0x" << std::hex << entry_filetype
      << std::dec << " for " << entry_name << "\n";
  }
}

static void
cmd_unpack(const std::string &path, const std::string &outdir)
{
  std::vector<std::byte> data = read_file(path);
  auto parsed = parse_plf(data);
  PLFHeader hdr = parsed.first;
  const std::vector<PLFEntry> &entries = parsed.second;

  std::filesystem::create_directories(outdir);

  json manifest;
  json h;

  h["magic"] = hdr.magic;
  h["hdr_version"] = hdr.hdr_version;
  h["header_size"] = hdr.header_size;
  h["entry_header_size"] = hdr.entry_header_size;
  h["unk_10"] = hdr.unk_10;
  h["unk_14"] = hdr.unk_14;
  h["unk_18"] = hdr.unk_18;
  h["unk_1C"] = hdr.unk_1C;
  h["header_crc_seed"] = hdr.header_crc_seed;
  h["version_major"] = hdr.version_major;
  h["version_minor"] = hdr.version_minor;
  h["version_bugfix"] = hdr.version_bugfix;
  h["unk_30"] = hdr.unk_30;
  h["file_size"] = hdr.file_size;

  manifest["header"] = h;
  manifest["entries"] = json::array();

  std::string extracted_root = (std::filesystem::path(outdir) / "extracted_rootfs").string(); std::filesystem::create_directories(extracted_root);

  for (const PLFEntry &e : entries) {
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << e.index
      << "_type_" << hex2(e.h.type)
      << ".bin";
    std::string file = oss.str();

    std::string out = (std::filesystem::path(outdir) / file).string();
    write_file(out, e.data);

    json je;

    je["index"] = e.index;
    je["type"] = e.h.type;
    je["size"] = e.h.size;
    je["crc"] = e.h.crc;
    je["extra"] = e.h.extra;
    je["usize"] = e.h.usize;
    je["file"] = file;

    if (e.h.type == 9) {
      std::vector<std::byte> dec;

      if (is_gzip(e.data) && e.h.usize != 0) {
        try {
          dec = gzip_decompress(e.data, e.h.usize);
          std::ostringstream decoss;
          decoss << std::setw(2) << std::setfill('0') << e.index
            << "_type_09.dec";
          std::string decfile = (std::filesystem::path(outdir) / decoss.str()).string();
          write_file(decfile, dec);
          je["dec_file"] = decoss.str();
        } catch (...) {
          std::cerr << "warn: failed to decode type-09 entry index "
            << e.index << ", keeping only raw blob\n";
          dec = e.data;
        }
      } else {
        dec = e.data;
      }

      if (!dec.empty()) {
        extract_type09_entry(dec, extracted_root, e.index);
      }
    }
    manifest["entries"].push_back(je);
  }

  std::string man = (std::filesystem::path(outdir) / "manifest.json").string();
  std::ofstream f(man);
  if (!f) {
    std::cerr << "err: cannot write manifest.json\n";
    std::exit(1);
  }

  f << manifest.dump(2);
  if (!f) {
    std::cerr << "err: write error manifest.json\n";
    std::exit(1);
  }
}

static void
add_entry_to_archive(std::vector<std::byte>& result,
    const std::filesystem::path& base_dir,
    const std::filesystem::path& entry_path)
{
  std::string relative_path = entry_path.string();
  if (relative_path.compare(0, base_dir.string().length(), base_dir.string()) == 0) {
    relative_path = relative_path.substr(base_dir.string().length());
  }
  if (relative_path[0] == '/') {
    relative_path = relative_path.substr(1);
  }

  for (char c : relative_path) {
    result.push_back(static_cast<std::byte>(c));
  }
  result.push_back(static_cast<std::byte>(0));

  std::error_code ec;
  auto status = std::filesystem::symlink_status(entry_path, ec);

  uint32_t mode_bits = static_cast<uint32_t>(status.permissions()) & 07777;
  uint32_t flags = 0;
  if (std::filesystem::is_symlink(status))
    flags = 0xA000 | mode_bits;
  else if (std::filesystem::is_directory(status))
    flags = 0x4000 | mode_bits;
  else if (std::filesystem::is_regular_file(status))
    flags = 0x8000 | mode_bits;
  else
    return;

  for (int i = 0; i < 4; ++i)
    result.push_back(static_cast<std::byte>((flags >> (8 * i)) & 0xFF));

  // The two words after the mode are always zero in stock firmware
  for (int i = 0; i < 8; ++i)
    result.push_back(static_cast<std::byte>(0));

  if (std::filesystem::is_symlink(status)) {
    auto target = std::filesystem::read_symlink(entry_path, ec);

    std::string target_str = target.string();
    for (char c : target_str) {
      result.push_back(static_cast<std::byte>(c));
    }

    result.push_back(static_cast<std::byte>(0));
  } else if (std::filesystem::is_regular_file(status)) {
    std::vector<std::byte> file_data = read_file(entry_path.string());
    result.insert(result.end(), file_data.begin(), file_data.end());
  }
}

static std::vector<std::byte>
rebuild_dec_from_extracted_rootfs(const std::filesystem::path& extracted_root,
                                    int entry_index)
{
  std::vector<std::byte> result;

  std::error_code ec;

  std::filesystem::path entry_root = extracted_root / ("entry_" + std::to_string(entry_index));

  if (!std::filesystem::exists(entry_root, ec)) {
    return result;
  }

  std::vector<std::filesystem::path> dirs;
  std::vector<std::filesystem::path> files;
  std::vector<std::filesystem::path> symlinks;

  for (const auto& iter_entry : std::filesystem::recursive_directory_iterator(entry_root, ec)) {
    if (ec) continue;

    auto status = iter_entry.symlink_status();

    if (std::filesystem::is_symlink(status)) {
      symlinks.push_back(iter_entry.path());
    } else if (std::filesystem::is_regular_file(status)) {
      files.push_back(iter_entry.path());
    } else if (std::filesystem::is_directory(status) &&
        std::filesystem::is_empty(iter_entry.path(), ec)) {
      dirs.push_back(iter_entry.path());
    }
    // The dirs with children come back on their own from the paths under them
  }

  std::sort(dirs.begin(), dirs.end());
  for (const auto& dir : dirs) {
    add_entry_to_archive(result, entry_root, dir);
  }

  // A sort keeps the output the same from run to run
  std::sort(files.begin(), files.end());
  for (const auto& file : files) {
    add_entry_to_archive(result, entry_root, file);
  }

  // The symlinks go last and get sorted for the same reason
  std::sort(symlinks.begin(), symlinks.end());
  for (const auto& symlink : symlinks) {
    add_entry_to_archive(result, entry_root, symlink);
  }

  return result;
}

static void
cmd_pack(const std::string &manifest, const std::string &out, bool use_original)
{
  json j = read_json(manifest);

  PLFHeader hdr;

  hdr.magic = j["header"]["magic"];
  hdr.hdr_version = j["header"]["hdr_version"];
  hdr.header_size = 0x38;
  hdr.entry_header_size = 0x14;
  hdr.unk_10 = j["header"]["unk_10"];
  hdr.unk_14 = j["header"]["unk_14"];
  hdr.unk_18 = j["header"]["unk_18"];
  hdr.unk_1C = j["header"]["unk_1C"];
  hdr.header_crc_seed = 0;
  hdr.version_major = j["header"]["version_major"];
  hdr.version_minor = j["header"]["version_minor"];
  hdr.version_bugfix = j["header"]["version_bugfix"];
  hdr.unk_30 = j["header"]["unk_30"];
  hdr.file_size = 0;

  std::vector<PLFEntry> entries;

  uint32_t offset = hdr.header_size;
  json arr = j["entries"];

  std::filesystem::path manifest_dir = std::filesystem::path(manifest).parent_path();
  std::filesystem::path extracted_root = manifest_dir / "extracted_rootfs";

  for (size_t i = 0; i < arr.size(); i++) {
    const json &je = arr[i];

    std::vector<std::byte> data;
    uint32_t usize = 0;

    uint32_t type = je["type"];

    std::filesystem::path entry_root =
      extracted_root / ("entry_" + std::to_string(je["index"].get<int>()));

    if (type == 9 && !use_original && std::filesystem::exists(entry_root)) {
      // This picks up any edits made to the unpacked files
      std::vector<std::byte> plain =
        rebuild_dec_from_extracted_rootfs(extracted_root, je["index"]);

      // The original entry might be stored raw so only gzip when it wasn't
      if (je["usize"] != 0) {
        data = gzip_compress(plain);
        usize = static_cast<uint32_t>(plain.size());
      } else {
        data = plain;
      }
    } else {
      std::string file = (manifest_dir / je["file"].get<std::string>()).string();
      data = read_file(file);
      usize = je["usize"];
    }

    PLFEntry e;

    e.index = static_cast<int>(i);
    e.h.type = type;
    e.h.size = static_cast<uint32_t>(data.size());
    e.h.crc = 0;
    e.h.extra = je["extra"];
    e.h.usize = usize;
    e.offset = offset;
    e.data_offset = offset + hdr.entry_header_size;
    e.data = data;

    entries.push_back(e);

    uint32_t total = hdr.entry_header_size + e.h.size;
    offset = (offset + total + 3) & ~3;
  }

  for (PLFEntry &e : entries) {
    uint32_t crc = 0;
    uint32_t count = 0;
    crc_update(crc, count, e.data);
    e.h.crc = crc_finalize(crc, count);
  }

  hdr.file_size = offset;

  std::vector<std::byte> image = hdr.pack();
  image.reserve(hdr.file_size);

  offset = hdr.header_size;

  for (PLFEntry &e : entries) {
    e.offset = offset;
    e.data_offset = offset + hdr.entry_header_size;

    std::vector<std::byte> eh = e.h.pack();
    image.insert(image.end(), eh.begin(), eh.end());
    image.insert(image.end(), e.data.begin(), e.data.end());

    uint32_t total = hdr.entry_header_size + e.h.size;
    uint32_t pad = (-static_cast<int32_t>(total)) & 3;
    for (uint32_t i = 0; i < pad; i++)
      image.push_back(std::byte{0});

    offset = offset + total + pad;
  }

  PLFHeader fh = PLFHeader::parse(image.data());
  uint32_t crc = recompute_header_crc(image, fh);
  hdr.header_crc_seed = crc;

  std::memcpy(image.data() + 0x20, &hdr.header_crc_seed, 4);

  uint32_t fsize = static_cast<uint32_t>(image.size());
  std::memcpy(image.data() + 0x34, &fsize, 4);

  write_file(out, image);
}


int
main(int argc, char **argv)
{
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  std::string prog = argv[0];
  std::string cmd = argv[1];

  if (cmd == "info") {
    if (argc != 3) {
      print_usage(prog);
      return 1;
    }
    cmd_info(argv[2]);
    return 0;
  }

  if (cmd == "unpack") {
    if (argc != 4) {
      print_usage(prog);
      return 1;
    }
    cmd_unpack(argv[2], argv[3]);
    return 0;
  }

  if (cmd == "pack") {
    bool use_original = false;
    size_t arg_offset = 2;

    if (argc >= 4 && std::string(argv[2]) == "--original") {
      use_original = true;
      arg_offset = 3;
    }

    if (argc != static_cast<int>(arg_offset) + 2) {
      print_usage(prog);
      return 1;
    }
    cmd_pack(argv[arg_offset], argv[arg_offset + 1], use_original);
    return 0;
  }

  print_usage(prog);
  return 1;
}

