//
// Font-loading QA harness for JuicySF Rack.
//
// 1) Unit-tests Source/DlsRepair.h (the exact repair code the plugin ships).
// 2) Stress-loads every SF2/SF3/DLS in the given directories through the same
//    pipeline the plugin uses: header sniff -> repair-to-temp if malformed DLS ->
//    fluid_synth_sfload with 7th-order interpolation. Reports per-file results.
//
// Build:
//   clang++ -std=c++17 tools/font_qa.cpp $(pkg-config --cflags --libs fluidsynth) -o /tmp/font_qa
// Run:
//   /tmp/font_qa <dir-or-file> [more dirs/files...]
//
#include "../Source/DlsRepair.h"
#include <fluidsynth.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using juicysf::repairDlsImage;

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  PASS  %s\n", name); \
    else { printf("  FAIL  %s\n", name); ++failures; } \
} while (0)

// ---------- unit tests for repairDlsImage ----------
static void put32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    v[o]=x&0xff; v[o+1]=(x>>8)&0xff; v[o+2]=(x>>16)&0xff; v[o+3]=(x>>24)&0xff;
}
static uint32_t get32(const std::vector<uint8_t>& v, size_t o) {
    return v[o] | (v[o+1]<<8) | (v[o+2]<<16) | ((uint32_t)v[o+3]<<24);
}
// tiny synthetic DLS: RIFF(DLS ) { colh(4), LIST(INFO){ INAM(4) } }
static std::vector<uint8_t> makeDls(bool undersizeInfo, bool oversizeOuter) {
    std::vector<uint8_t> v(12 + 12 + 12 + 12, 0);
    memcpy(&v[0], "RIFF", 4); memcpy(&v[8], "DLS ", 4);
    memcpy(&v[12], "colh", 4); put32(v, 16, 4); put32(v, 20, 2); // colh, 2 instruments
    memcpy(&v[24], "LIST", 4); put32(v, 28, 16); memcpy(&v[32], "INFO", 4);
    memcpy(&v[36], "INAM", 4); put32(v, 40, 4); memcpy(&v[44], "Abcd", 4);
    put32(v, 4, (uint32_t)v.size() - 8);
    if (undersizeInfo) put32(v, 28, 8);                    // INFO LIST claims 8, really 16
    if (oversizeOuter) put32(v, 4, (uint32_t)v.size() + 40); // outer RIFF overshoots EOF
    return v;
}

static void unitTests() {
    printf("== repairDlsImage unit tests ==\n");
    { // too small / empty
        std::vector<uint8_t> v;
        CHECK(!repairDlsImage(v.data(), v.size()), "empty buffer: untouched");
        std::vector<uint8_t> w(8, 0);
        CHECK(!repairDlsImage(w.data(), w.size()), "8-byte buffer: untouched");
    }
    { // non-DLS RIFF (an SF2) must never be modified
        std::vector<uint8_t> v(64, 0xAB);
        memcpy(&v[0], "RIFF", 4); put32(v, 4, 999999); memcpy(&v[8], "sfbk", 4);
        auto before = v;
        CHECK(!repairDlsImage(v.data(), v.size()) && v == before, "SF2 image: untouched even with bad size");
    }
    { // well-formed DLS: untouched
        auto v = makeDls(false, false); auto before = v;
        CHECK(!repairDlsImage(v.data(), v.size()) && v == before, "well-formed DLS: untouched");
    }
    { // Awave-style undersized inner LIST -> phantom chunk past EOF
        auto v = makeDls(true, false);
        bool changed = repairDlsImage(v.data(), v.size());
        CHECK(changed && get32(v, 28) == 16, "undersized INFO LIST: size corrected to reach EOF");
        // idempotent: second run does nothing
        auto once = v;
        CHECK(!repairDlsImage(v.data(), v.size()) && v == once, "repair is idempotent");
    }
    { // outer RIFF size overshooting EOF
        auto v = makeDls(false, true);
        bool changed = repairDlsImage(v.data(), v.size());
        CHECK(changed && get32(v, 4) == v.size() - 8, "oversized outer RIFF: clamped to file size");
    }
    { // truncated: file cut mid-chunk (first chunk oversized) -> only outer clamp is possible
        auto v = makeDls(false, false);
        put32(v, 16, 4000); // colh claims 4000 bytes
        repairDlsImage(v.data(), v.size());
        CHECK(get32(v, 4) == v.size() - 8, "truncated-mid-first-chunk: outer size still sane");
    }
}

// ---------- corpus loader (mirrors FluidSynthModel::loadFont) ----------
struct Stats { int ok=0, repaired=0, failed=0; std::vector<std::string> failedFiles; };

static void tryLoad(const fs::path& p, Stats& st) {
    // sniff
    std::ifstream in(p, std::ios::binary);
    char hdr[12] = {};
    in.read(hdr, 12);
    const bool isDls = !memcmp(hdr, "RIFF", 4) && !memcmp(hdr + 8, "DLS ", 4);

    fs::path toLoad = p;
    bool repaired = false;
    std::vector<uint8_t> img;
    if (isDls) {
        std::ifstream f(p, std::ios::binary);
        img.assign(std::istreambuf_iterator<char>(f), {});
        if (repairDlsImage(img.data(), img.size())) {
            repaired = true;
            toLoad = fs::temp_directory_path() / "font_qa_repaired.dls";
            std::ofstream o(toLoad, std::ios::binary);
            o.write((const char*) img.data(), (std::streamsize) img.size());
        }
    }

    fluid_settings_t* s = new_fluid_settings();
    fluid_synth_t* syn = new_fluid_synth(s);
    fluid_synth_set_interp_method(syn, -1, FLUID_INTERP_HIGHEST);
    int id = fluid_synth_sfload(syn, toLoad.string().c_str(), 1);
    int presets = 0;
    if (id != -1) {
        fluid_sfont_t* sf = fluid_synth_get_sfont_by_id(syn, id);
        fluid_sfont_iteration_start(sf);
        while (fluid_sfont_iteration_next(sf)) presets++;
    }
    delete_fluid_synth(syn); delete_fluid_settings(s);

    if (id != -1 && presets > 0) {
        printf("  OK    %-50s presets=%-4d%s\n", p.filename().string().c_str(), presets,
               repaired ? "  [auto-repaired]" : "");
        st.ok++; if (repaired) st.repaired++;
    } else if (id != -1) {
        printf("  WARN  %-50s loaded but 0 presets\n", p.filename().string().c_str());
        st.failed++; st.failedFiles.push_back(p.string() + " (0 presets)");
    } else {
        printf("  FAIL  %-50s%s\n", p.filename().string().c_str(),
               repaired ? "  (even after repair)" : "");
        st.failed++; st.failedFiles.push_back(p.string());
    }
}

int main(int argc, char** argv) {
    unitTests();
    Stats st;
    printf("== corpus load test ==\n");
    for (int i = 1; i < argc; i++) {
        fs::path root{argv[i]};
        if (fs::is_directory(root)) {
            std::vector<fs::path> files;
            for (auto& e : fs::directory_iterator(root)) {
                auto ext = e.path().extension().string();
                for (auto& c : ext) c = (char) tolower(c);
                if (ext == ".sf2" || ext == ".sf3" || ext == ".dls") files.push_back(e.path());
            }
            std::sort(files.begin(), files.end());
            for (auto& f : files) tryLoad(f, st);
        } else if (fs::exists(root)) {
            tryLoad(root, st);
        }
    }
    printf("== summary: %d ok (%d auto-repaired), %d failed, %d unit-test failures ==\n",
           st.ok, st.repaired, st.failed, failures);
    for (auto& f : st.failedFiles) printf("  failed: %s\n", f.c_str());
    return (failures || st.failed) ? 1 : 0;
}
