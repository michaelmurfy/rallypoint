#include "ra2assets/fileio.hpp"
#include <fstream>
#include <stdexcept>

namespace ra2assets {

// Single block read. The old engine slurp
// (std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {})) pulled one
// byte per iterator increment with no reserve; on the 268 MB ra2.mix (read
// TWICE) + 195 MB ra2md.mix that byte-wise path measured 79 s for
// `ra2engine --screenshot` on the host. Seek-to-end + one read() makes the
// same load I/O-bound (seconds). The "cannot open file: " message prefix
// matches the old engine slurp exactly -- README's Android logcat
// diagnostics depend on that phrasing.
std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open file: " + path);
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) throw std::runtime_error("cannot stat file: " + path);
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    if (n > 0) {
        f.read(reinterpret_cast<char*>(buf.data()), n);
        if (f.gcount() != n) throw std::runtime_error("short read: " + path);
    }
    return buf;
}

}  // namespace ra2assets
