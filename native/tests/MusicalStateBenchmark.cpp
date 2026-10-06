#include "PrismForge/MusicalStateEngine.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>

namespace allocation_probe {
std::atomic_bool tracking{false};
std::atomic<std::uint64_t> calls{0};
std::atomic<std::uint64_t> bytes{0};

void Record(std::size_t size) noexcept {
  if (!tracking.load(std::memory_order_relaxed)) return;
  calls.fetch_add(1, std::memory_order_relaxed);
  bytes.fetch_add(static_cast<std::uint64_t>(size), std::memory_order_relaxed);
}
}  // namespace allocation_probe

void* operator new(std::size_t size) {
  const std::size_t actual = size == 0 ? 1 : size;
  void* memory = std::malloc(actual);
  if (memory == nullptr) throw std::bad_alloc{};
  allocation_probe::Record(size);
  return memory;
}

void* operator new[](std::size_t size) {
  return ::operator new(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using Clock = std::chrono::steady_clock;
using prismforge::MusicalStateEngine;
using prismforge::MusicalStateEngineConfig;
using prismforge::MusicalStateFrameV1;
using prismforge::SignalFrameV1;

constexpr std::uint64_t kSamplesPerUpdate = 800;  // 60 Hz at 48 kHz.
constexpr std::uint64_t kWarmupUpdates = 50000;
constexpr std::uint64_t kUpdatesPerBatch = 4096;
constexpr std::uint64_t kBatchCount = 256;
constexpr std::uint64_t kTimedSingleCalls = 50000;

struct ProcessMemory {
  std::uint64_t workingSetBytes = 0;
  std::uint64_t privateBytes = 0;
};

ProcessMemory ReadProcessMemory() noexcept {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (!GetProcessMemoryInfo(
          GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
          sizeof(counters))) {
    return {};
  }
  return {static_cast<std::uint64_t>(counters.WorkingSetSize),
          static_cast<std::uint64_t>(counters.PrivateUsage)};
}

SignalFrameV1 InitialSignal() noexcept {
  SignalFrameV1 signal{};
  signal.rms = 0.18f;
  signal.peak = 0.42f;
  signal.bass = 0.33f;
  signal.mids = 0.24f;
  signal.highs = 0.16f;
  signal.bpm = 120.0f;
  signal.beatConfidence = 0.8f;
  for (std::size_t i = 0; i < signal.bands.size(); ++i) {
    signal.bands[i] = 0.08f + static_cast<float>(i % 8) * 0.025f;
  }
  return signal;
}

void PrepareNext(SignalFrameV1& signal, std::uint64_t ordinal) noexcept {
  signal.sampleIndex += kSamplesPerUpdate;
  signal.hit = ordinal % 30 == 0;
  signal.accent = ordinal % 120 == 0;
  if (signal.hit) ++signal.hitCount;
  if (signal.accent) ++signal.accentCount;
  signal.beatPhase = static_cast<float>(ordinal % 30) / 30.0f;
}

std::uint64_t MixChecksum(std::uint64_t checksum,
                          const MusicalStateFrameV1& frame) noexcept {
  checksum ^= frame.sourceSampleIndex + UINT64_C(0x9e3779b97f4a7c15) +
      (checksum << 6) + (checksum >> 2);
  checksum ^= static_cast<std::uint64_t>(
      std::bit_cast<std::uint32_t>(frame.grooveEnergy)) << 32;
  checksum ^= std::bit_cast<std::uint32_t>(frame.building);
  checksum ^= frame.eventId;
  return checksum;
}

std::int64_t Difference(std::uint64_t after, std::uint64_t before) noexcept {
  if (after >= before) {
    const std::uint64_t positive = after - before;
    return positive > static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(positive);
  }
  const std::uint64_t negative = before - after;
  return negative > static_cast<std::uint64_t>(
      std::numeric_limits<std::int64_t>::max())
      ? std::numeric_limits<std::int64_t>::min()
      : -static_cast<std::int64_t>(negative);
}
}  // namespace

int main() {
  MusicalStateEngineConfig config{};
  config.sampleRate = 48000;
  config.seed = UINT64_C(0x0123456789abcdef);
  MusicalStateEngine engine(config);
  SignalFrameV1 signal = InitialSignal();
  MusicalStateFrameV1 frame{};
  std::uint64_t ordinal = 1;
  std::uint64_t checksum = 0;

  for (std::uint64_t i = 0; i < kWarmupUpdates; ++i, ++ordinal) {
    PrepareNext(signal, ordinal);
    frame = engine.Advance(signal);
  }

  std::chrono::nanoseconds minimumTimerPair =
      std::chrono::nanoseconds::max();
  for (unsigned i = 0; i < 10000; ++i) {
    const auto begin = Clock::now();
    const auto end = Clock::now();
    minimumTimerPair = std::min(
        minimumTimerPair,
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin));
  }

  const ProcessMemory memoryBefore = ReadProcessMemory();
  allocation_probe::calls = 0;
  allocation_probe::bytes = 0;
  allocation_probe::tracking = true;

  std::chrono::nanoseconds worstBatch{};
  const auto overallBegin = Clock::now();
  for (std::uint64_t batch = 0; batch < kBatchCount; ++batch) {
    const auto batchBegin = Clock::now();
    for (std::uint64_t i = 0; i < kUpdatesPerBatch; ++i, ++ordinal) {
      PrepareNext(signal, ordinal);
      frame = engine.Advance(signal);
    }
    const auto batchElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - batchBegin);
    worstBatch = std::max(worstBatch, batchElapsed);
    checksum = MixChecksum(checksum, frame);
  }
  const auto overallElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      Clock::now() - overallBegin);

  std::chrono::nanoseconds worstObservedCall{};
  for (std::uint64_t i = 0; i < kTimedSingleCalls; ++i, ++ordinal) {
    PrepareNext(signal, ordinal);
    const auto callBegin = Clock::now();
    frame = engine.Advance(signal);
    const auto callElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - callBegin);
    worstObservedCall = std::max(worstObservedCall, callElapsed);
  }
  checksum = MixChecksum(checksum, frame);

  allocation_probe::tracking = false;
  const ProcessMemory memoryAfter = ReadProcessMemory();

  const std::uint64_t measuredUpdates = kUpdatesPerBatch * kBatchCount;
  const double averageNs = static_cast<double>(overallElapsed.count()) /
      static_cast<double>(measuredUpdates);
  const double worstBatchAverageNs = static_cast<double>(worstBatch.count()) /
      static_cast<double>(kUpdatesPerBatch);

  std::cout << std::fixed << std::setprecision(2)
            << "updates=" << measuredUpdates << '\n'
            << "average_ns_per_update=" << averageNs << '\n'
            << "worst_batch_average_ns_per_update=" << worstBatchAverageNs << '\n'
            << "worst_observed_call_ns=" << worstObservedCall.count() << '\n'
            << "minimum_timer_pair_ns=" << minimumTimerPair.count() << '\n'
            << "operator_new_calls=" << allocation_probe::calls.load() << '\n'
            << "operator_new_bytes=" << allocation_probe::bytes.load() << '\n'
            << "signal_frame_bytes=" << sizeof(SignalFrameV1) << '\n'
            << "musical_frame_bytes=" << sizeof(MusicalStateFrameV1) << '\n'
            << "musical_engine_bytes=" << sizeof(MusicalStateEngine) << '\n'
            << "working_set_delta_bytes="
            << Difference(memoryAfter.workingSetBytes,
                          memoryBefore.workingSetBytes) << '\n'
            << "private_bytes_delta="
            << Difference(memoryAfter.privateBytes,
                          memoryBefore.privateBytes) << '\n'
            << "checksum=" << checksum << '\n';
  return checksum == 0 ? 1 : 0;
}
