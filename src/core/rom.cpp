#include "rom.h"
#include "sha256.h"

#include <cstdio>

namespace sm64nv {

RomCheck check_rom(const std::vector<uint8_t> &rom) {
    char reason[160];
    if (rom.size() != kUsRomSize) {
        snprintf(reason, sizeof reason, "size=%zu want=%zu", rom.size(), kUsRomSize);
        return {false, reason, ""};
    }
    std::string hash = sha256_hex(rom);
    if (hash != kUsRomSha256) {
        snprintf(reason, sizeof reason, "sha256=%s want=%s", hash.c_str(), kUsRomSha256);
        return {false, reason, hash};
    }
    return {true, "", hash};
}

std::vector<uint8_t> read_file(const std::string &path) {
    std::vector<uint8_t> out;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return out;
    uint8_t buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return out;
}

bool write_file(const std::string &path, const std::vector<uint8_t> &bytes) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return fclose(f) == 0 && ok;
}

}
