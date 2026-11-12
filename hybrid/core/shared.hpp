// The block of memory the engine shares with its controllers (the Node add-on, the Qt bridge).
// Controllers write parameters and bump paramGen; the engine writes meters. Everything is a
// lock-free atomic, so no side ever waits for another.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace amfb {

constexpr int kStrips = 16;
constexpr int kBands = 7;
constexpr std::uint32_t kMagic = 0x414D4642;   // "AMFB"
constexpr std::uint32_t kVersion = 2;
constexpr float kDefaultFreq[kBands] = {60.f, 150.f, 400.f, 1000.f, 2500.f, 6000.f, 12000.f};

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free, "the shared block needs lock-free atomics");

struct StripParams {
  std::atomic<float> gainDb, pan, width;        // -90..+12 dB, -1..+1, 0 (mono) .. 2 (wide)
  std::atomic<std::uint32_t> mute, solo, eqOn;
  std::atomic<float> compLevel;                 // 0 = off .. 10 = hardest; set by the user only, never detected
  std::atomic<float> eqFreq[kBands], eqGain[kBands], eqQ[kBands];
};
struct StripMeters {                            // linear values, 1.0 = 0 dBFS
  std::atomic<float> peakL, peakR, rmsL, rmsR, truePeak, hold, phase;   // phase: -1..+1 correlation
  std::atomic<float> reductionDb;               // compressor gain reduction, 0 or negative
  std::atomic<std::uint32_t> active;            // 1 while the source delivers audio
  char source[32];                              // written once by the engine before audio starts
};
struct SharedBlock {
  std::uint32_t magic, version;
  std::atomic<std::uint32_t> paramGen;          // bumped by a controller after it wrote parameters
  std::atomic<std::uint64_t> heartbeat;         // frames processed; stands still when the engine is gone
  std::atomic<std::uint32_t> underruns;         // blocks a capture ring could not fill
  std::atomic<float> sampleRate, masterGainDb;
  StripParams p[kStrips];
  StripMeters m[kStrips + 1];                   // [kStrips] is the master bus
};

inline void initBlock(SharedBlock* b) {
  new (b) SharedBlock{};
  b->version = kVersion;
  b->masterGainDb = 0.f;
  for (auto& s : b->p) {
    s.gainDb = 0.f; s.pan = 0.f; s.width = 1.f; s.mute = 0; s.solo = 0; s.eqOn = 1; s.compLevel = 0.f;
    for (int k = 0; k < kBands; ++k) { s.eqFreq[k] = kDefaultFreq[k]; s.eqGain[k] = 0.f; s.eqQ[k] = 0.9f; }
  }
  b->magic = kMagic;                            // last: a reader that sees the magic sees a whole block
}

inline std::string sharedName() {
  if (const char* e = std::getenv("AMFB_SHM"); e && *e) return e;
#ifdef _WIN32
  return "Local\\AMFB_Mixer_SharedMem";         // Local\ = this logon session only
#else
  return "/AMFB_Mixer_SharedMem";
#endif
}

class SharedMem {
 public:
  SharedMem() = default;
  SharedMem(const SharedMem&) = delete;
  SharedMem& operator=(const SharedMem&) = delete;
  ~SharedMem() { close(); }

  // create = the engine (makes and initialises the block); otherwise attach to a running engine.
  SharedBlock* open(bool create, std::string* err = nullptr) {
    close();
    const std::string name = sharedName();
#ifdef _WIN32
    h_ = create ? CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedBlock), name.c_str())
                : OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!h_) { if (err) *err = create ? "cannot create the shared memory" : "the engine is not running"; return nullptr; }
    b_ = static_cast<SharedBlock*>(MapViewOfFile(h_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock)));
#else
    const int fd = shm_open(name.c_str(), create ? (O_CREAT | O_RDWR) : O_RDWR, 0600);   // owner only
    if (fd < 0) { if (err) *err = create ? "cannot create the shared memory" : "the engine is not running"; return nullptr; }
    if (create && ftruncate(fd, sizeof(SharedBlock)) != 0) { ::close(fd); if (err) *err = "cannot size the shared memory"; return nullptr; }
    void* p = mmap(nullptr, sizeof(SharedBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    b_ = p == MAP_FAILED ? nullptr : static_cast<SharedBlock*>(p);
    owner_ = create;
#endif
    if (!b_) { if (err) *err = "cannot map the shared memory"; close(); return nullptr; }
    if (create) initBlock(b_);
    else if (b_->magic != kMagic || b_->version != kVersion) { if (err) *err = "the shared memory belongs to another version"; close(); return nullptr; }
    return b_;
  }
  void close() {
#ifdef _WIN32
    if (b_) UnmapViewOfFile(b_);
    if (h_) CloseHandle(h_);
    h_ = nullptr;
#else
    if (b_) munmap(b_, sizeof(SharedBlock));
    if (owner_) shm_unlink(sharedName().c_str());
    owner_ = false;
#endif
    b_ = nullptr;
  }
  SharedBlock* get() const { return b_; }

 private:
  SharedBlock* b_ = nullptr;
#ifdef _WIN32
  HANDLE h_ = nullptr;
#else
  bool owner_ = false;
#endif
};

}  // namespace amfb
