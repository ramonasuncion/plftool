#include <iostream>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <fstream>
#include "plf_crc_table.hpp"

static std::vector<std::byte> read_file(const std::string &path)
{
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::cerr << "err: cannot open file '" << path << "'\n";
    exit(1);
  }

  f.seekg(0, std::ios::end);
  std::streamsize s = f.tellg();
  f.seekg(0, std::ios::beg);

  std::vector<std::byte> out(s);
  if (s > 0)
    f.read(reinterpret_cast<char*>(out.data()), s);

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


