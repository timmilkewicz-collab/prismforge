#include "MidiBridge.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string_view>
#include <thread>

namespace prismforge {
namespace {
constexpr std::uint8_t kNote = 0x90;
constexpr std::uint8_t kControl = 0xB0;
constexpr std::uint8_t kDark = 12;
constexpr std::uint8_t kDimRed = 13;
constexpr std::uint8_t kRed = 15;
constexpr std::uint8_t kDimGreen = 28;
constexpr std::uint8_t kGreen = 60;
constexpr std::uint8_t kDimAmber = 29;
constexpr std::uint8_t kAmber = 63;
constexpr std::uint8_t kDimOrange = 30;
constexpr std::uint8_t kOrange = 47;
constexpr std::uint8_t kDimYellow = 45;
constexpr std::uint8_t kYellow = 62;
constexpr std::uint32_t kSaveHoldMs = 1200;

int PhysicalSlot(std::uint8_t command, std::uint8_t number) noexcept {
  if (command == kControl) {
    return number >= 104 && number <= 111 ? 72 + number - 104 : -1;
  }
  if (command == kNote && number / 16 < 8 && number % 16 < 9) {
    return (number / 16) * 9 + number % 16;
  }
  return -1;
}

std::pair<std::uint8_t, std::uint8_t> SlotMessage(std::size_t slot) noexcept {
  if (slot >= 72) return {kControl, static_cast<std::uint8_t>(104 + slot - 72)};
  return {kNote, static_cast<std::uint8_t>((slot / 9) * 16 + slot % 9)};
}

const LaunchpadBinding* FindBinding(std::uint8_t command,
                                    std::uint8_t number) noexcept {
  for (const auto& binding : LaunchpadSBindings()) {
    if (binding.command == command && binding.number == number) return &binding;
  }
  return nullptr;
}

std::string Utf8(const wchar_t* wide) {
  const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0,
                                         nullptr, nullptr);
  if (length <= 1) return {};
  std::string value(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, value.data(), length,
                      nullptr, nullptr);
  value.pop_back();
  return value;
}

bool LaunchpadName(std::string_view name) {
  std::string lower(name);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lower.find("launchpad s") != std::string::npos;
}

bool PrismBurstLikelyActive() {
  // The source build may run as javaw.exe, so process names alone are not
  // enough. A fresh status file is a deliberately conservative second gate.
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
      do {
        std::wstring name(entry.szExeFile);
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t ch) {
          return static_cast<wchar_t>(std::towlower(ch));
        });
        if (name.find(L"prismburst") != std::wstring::npos) {
          CloseHandle(snapshot);
          return true;
        }
      } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
  }
  const DWORD needed = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (needed < 2 || needed > 32768) return false;
  std::wstring appData(needed, L'\0');
  const DWORD copied = GetEnvironmentVariableW(L"APPDATA", appData.data(), needed);
  if (copied == 0 || copied >= needed) return false;
  appData.resize(copied);
  std::error_code ec;
  const auto statusPath = std::filesystem::path(appData) / L"PrismBurst" / L"status.json";
  const auto modified = std::filesystem::last_write_time(statusPath, ec);
  if (ec) return false;
  const auto age = std::filesystem::file_time_type::clock::now() - modified;
  return age >= decltype(age)::zero() && age < std::chrono::seconds(10);
}

std::string WinMmError(std::string_view operation, MMRESULT result) {
  return std::string(operation) + " failed (WinMM " + std::to_string(result) + ")";
}
}  // namespace

const std::array<LaunchpadBinding, 50>& LaunchpadSBindings() noexcept {
  static const auto bindings = [] {
    std::array<LaunchpadBinding, 50> result{};
    std::size_t next = 0;
    const auto add = [&](std::uint8_t command, std::uint8_t number,
                         LaunchpadActionType type, unsigned deck, unsigned index,
                         float value, bool guarded = false) {
      result[next++] = {command, number, {type, deck, index, value}, guarded};
    };
    for (unsigned scene = 0; scene < 12; ++scene) {
      const auto aNote = static_cast<std::uint8_t>(
          scene < 8 ? scene : 16 + scene - 8);
      const auto bNote = static_cast<std::uint8_t>(
          scene < 8 ? 32 + scene : 48 + scene - 8);
      add(kNote, aNote, LaunchpadActionType::SetScene, 0, scene, 0.0f);
      add(kNote, bNote, LaunchpadActionType::SetScene, 1, scene, 0.0f);
    }
    for (unsigned cue = 0; cue < 8; ++cue) {
      add(kNote, static_cast<std::uint8_t>(64 + cue),
          LaunchpadActionType::RecallCue, 0, cue, 0.0f);
      add(kNote, static_cast<std::uint8_t>(8 + cue * 16),
          LaunchpadActionType::SaveCue, 0, cue, 0.0f, true);
      add(kControl, static_cast<std::uint8_t>(104 + cue),
          LaunchpadActionType::SetCrossfader, 0, cue, cue / 7.0f);
    }
    add(kNote, 80, LaunchpadActionType::ToggleBlackout, 0, 0, 0.0f);
    add(kNote, 81, LaunchpadActionType::TogglePanicDim, 0, 0, 0.0f);
    return result;
  }();
  return bindings;
}

bool ValidateLaunchpadSProfile() noexcept {
  std::bitset<80> seen;
  for (const auto& binding : LaunchpadSBindings()) {
    const int slot = PhysicalSlot(binding.command, binding.number);
    if (slot < 0 || seen.test(static_cast<std::size_t>(slot))) return false;
    seen.set(static_cast<std::size_t>(slot));
  }
  return seen.count() == LaunchpadSBindings().size();
}

LaunchpadLedFrame BuildLaunchpadSLeds(const ShowSnapshot& show,
                                     const std::array<bool, 8>& cueSaved,
                                     const std::bitset<80>& pressed) noexcept {
  LaunchpadLedFrame frame{};
  frame.fill(kDark);
  const float crossfader = std::isfinite(show.crossfader)
      ? std::clamp(show.crossfader, 0.0f, 1.0f) : 0.0f;
  const unsigned selectedCrossfader = static_cast<unsigned>(std::lround(crossfader * 7.0f));
  for (const auto& binding : LaunchpadSBindings()) {
    const auto slot = static_cast<std::size_t>(PhysicalSlot(binding.command, binding.number));
    const bool held = pressed.test(slot);
    std::uint8_t color = kDark;
    switch (binding.action.type) {
      case LaunchpadActionType::SetScene:
        color = show.decks[binding.action.deck].sceneId ==
                kSceneIds[binding.action.index] ? kGreen : kDimGreen;
        if (held) color = kGreen;
        break;
      case LaunchpadActionType::RecallCue:
        color = cueSaved[binding.action.index] ?
            (held ? kYellow : kDimYellow) : kDark;
        break;
      case LaunchpadActionType::SaveCue:
        color = held ? kRed : kDimRed;
        break;
      case LaunchpadActionType::SetCrossfader:
        color = binding.action.index == selectedCrossfader || held
            ? kOrange : kDimOrange;
        break;
      case LaunchpadActionType::ToggleBlackout:
        color = show.blackout || held ? kRed : kDimRed;
        break;
      case LaunchpadActionType::TogglePanicDim:
        color = show.panicDim || held ? kAmber : kDimAmber;
        break;
    }
    frame[slot] = color;
  }
  return frame;
}

std::optional<LaunchpadAction> LaunchpadSInterpreter::Process(
    const MidiMessage& message) noexcept {
  if ((message.status & 0x0F) != 0) return std::nullopt;
  std::uint8_t command = message.status & 0xF0;
  if (command == 0x80) command = kNote;
  if (command != kNote && command != kControl) return std::nullopt;
  const auto* binding = FindBinding(command, message.data1);
  if (!binding) return std::nullopt;
  const auto slot = static_cast<std::size_t>(PhysicalSlot(command, message.data1));
  const bool down = (message.status & 0xF0) != 0x80 && message.data2 != 0;
  if (down) {
    if (pressed_.test(slot)) return std::nullopt;
    pressed_.set(slot);
    pressedAtMs_[slot] = message.timestampMs;
    if (!binding->guarded) return binding->action;
    return std::nullopt;
  }
  if (!pressed_.test(slot)) return std::nullopt;
  pressed_.reset(slot);
  if (binding->guarded &&
      static_cast<std::uint32_t>(message.timestampMs - pressedAtMs_[slot]) >=
          kSaveHoldMs) {
    return binding->action;
  }
  return std::nullopt;
}

void LaunchpadSInterpreter::Reset() noexcept {
  pressed_.reset();
  pressedAtMs_.fill(0);
}

struct MidiBridge::Impl {
  explicit Impl(BoundedQueue<MidiMessage>& queue) : inputQueue(queue) {}
  BoundedQueue<MidiMessage>& inputQueue;
  BoundedQueue<LaunchpadLedFrame> feedbackQueue{4};
  HMIDIIN input = nullptr;
  HMIDIOUT output = nullptr;
  std::atomic_bool outputRunning{false};
  std::thread outputThread;
  std::optional<LaunchpadLedFrame> lastQueued;
  std::atomic_uint64_t droppedInput{0};
  std::atomic_uint64_t droppedFeedback{0};

  static void CALLBACK OnMidi(HMIDIIN, UINT event, DWORD_PTR instance,
                              DWORD_PTR data, DWORD_PTR timestamp) noexcept {
    if (event != MIM_DATA || instance == 0) return;
    auto* self = reinterpret_cast<Impl*>(instance);
    MidiMessage message{
        static_cast<std::uint8_t>(data & 0xFF),
        static_cast<std::uint8_t>((data >> 8) & 0xFF),
        static_cast<std::uint8_t>((data >> 16) & 0xFF),
        static_cast<std::uint32_t>(timestamp)};
    if (!self->inputQueue.TryPush(message)) ++self->droppedInput;
  }

  void OutputLoop() noexcept {
    LaunchpadLedFrame lastSent{};
    lastSent.fill(0xFF);  // First frame explicitly darkens all unmapped keys.
    std::optional<LaunchpadLedFrame> desired;
    while (outputRunning.load(std::memory_order_relaxed)) {
      while (auto frame = feedbackQueue.TryPop()) desired = std::move(*frame);
      if (desired && output) {
        for (std::size_t slot = 0; slot < desired->size(); ++slot) {
          if ((*desired)[slot] == lastSent[slot]) continue;
          const auto [command, number] = SlotMessage(slot);
          const DWORD packed = static_cast<DWORD>(command) |
              (static_cast<DWORD>(number) << 8) |
              (static_cast<DWORD>((*desired)[slot]) << 16);
          if (midiOutShortMsg(output, packed) == MMSYSERR_NOERROR) {
            lastSent[slot] = (*desired)[slot];
          } else {
            ++droppedFeedback;
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    // Only clear our LEDs when PrismBurst has not begun using the controller.
    if (output && desired && !PrismBurstLikelyActive()) {
      for (std::size_t slot = 0; slot < desired->size(); ++slot) {
        const auto [command, number] = SlotMessage(slot);
        const DWORD packed = static_cast<DWORD>(command) |
            (static_cast<DWORD>(number) << 8) | (static_cast<DWORD>(kDark) << 16);
        (void)midiOutShortMsg(output, packed);
      }
    }
  }
};

MidiBridge::MidiBridge(BoundedQueue<MidiMessage>& input)
    : impl_(std::make_unique<Impl>(input)) {}

MidiBridge::~MidiBridge() { Stop(); }

std::vector<MidiDeviceInfo> MidiBridge::InputDevices() {
  std::vector<MidiDeviceInfo> devices;
  for (UINT id = 0; id < midiInGetNumDevs(); ++id) {
    MIDIINCAPSW caps{};
    if (midiInGetDevCapsW(id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
      devices.push_back({id, Utf8(caps.szPname)});
    }
  }
  return devices;
}

std::vector<MidiDeviceInfo> MidiBridge::OutputDevices() {
  std::vector<MidiDeviceInfo> devices;
  for (UINT id = 0; id < midiOutGetNumDevs(); ++id) {
    MIDIOUTCAPSW caps{};
    if (midiOutGetDevCapsW(id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
      devices.push_back({id, Utf8(caps.szPname)});
    }
  }
  return devices;
}

bool MidiBridge::OpenInput(unsigned deviceId, std::string& error) {
  if (impl_->input) {
    error = "MIDI input is already open";
    return false;
  }
  const auto devices = InputDevices();
  const auto selected = std::find_if(devices.begin(), devices.end(),
      [deviceId](const MidiDeviceInfo& device) { return device.id == deviceId; });
  if (selected == devices.end()) {
    error = "MIDI input device ID is unavailable";
    return false;
  }
  if (LaunchpadName(selected->name) && PrismBurstLikelyActive()) {
    error = "PrismBurst appears active; Launchpad S is reserved for its live controls";
    return false;
  }
  const auto result = midiInOpen(&impl_->input, deviceId,
      reinterpret_cast<DWORD_PTR>(&Impl::OnMidi),
      reinterpret_cast<DWORD_PTR>(impl_.get()), CALLBACK_FUNCTION);
  if (result != MMSYSERR_NOERROR) {
    impl_->input = nullptr;
    error = WinMmError("midiInOpen", result);
    return false;
  }
  const auto startResult = midiInStart(impl_->input);
  if (startResult != MMSYSERR_NOERROR) {
    (void)midiInClose(impl_->input);
    impl_->input = nullptr;
    error = WinMmError("midiInStart", startResult);
    return false;
  }
  return true;
}

bool MidiBridge::OpenLaunchpadS(bool feedback, std::string& error) {
  if (impl_->input) {
    error = "MIDI input is already open";
    return false;
  }
  if (PrismBurstLikelyActive()) {
    error = "PrismBurst appears active; Launchpad S is reserved for its live controls";
    return false;
  }
  const auto inputs = InputDevices();
  const auto selectedInput = std::find_if(inputs.begin(), inputs.end(),
      [](const MidiDeviceInfo& device) { return LaunchpadName(device.name); });
  if (selectedInput == inputs.end()) {
    error = "Launchpad S MIDI input was not found";
    return false;
  }
  std::optional<unsigned> outputId;
  if (feedback) {
    const auto outputs = OutputDevices();
    const auto selectedOutput = std::find_if(outputs.begin(), outputs.end(),
        [](const MidiDeviceInfo& device) { return LaunchpadName(device.name); });
    if (selectedOutput == outputs.end()) {
      error = "Launchpad S MIDI output was not found";
      return false;
    }
    outputId = selectedOutput->id;
  }
  if (!OpenInput(selectedInput->id, error)) return false;
  if (outputId) {
    const auto result = midiOutOpen(&impl_->output, *outputId, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
      impl_->output = nullptr;
      error = WinMmError("midiOutOpen", result);
      Stop();
      return false;
    }
    // The note map is valid only in the documented X-Y layout. Simple buffer
    // mode ensures the per-key LED writes are visible immediately.
    const DWORD xyLayout = static_cast<DWORD>(kControl) | (1u << 16);
    const DWORD simpleBuffer = static_cast<DWORD>(kControl) | (32u << 16);
    if (midiOutShortMsg(impl_->output, xyLayout) != MMSYSERR_NOERROR ||
        midiOutShortMsg(impl_->output, simpleBuffer) != MMSYSERR_NOERROR) {
      error = "Launchpad S layout initialization failed";
      Stop();
      return false;
    }
    impl_->outputRunning = true;
    impl_->outputThread = std::thread([this] { impl_->OutputLoop(); });
  }
  return true;
}

void MidiBridge::Stop() noexcept {
  if (impl_->input) {
    (void)midiInStop(impl_->input);
    (void)midiInReset(impl_->input);
    (void)midiInClose(impl_->input);
    impl_->input = nullptr;
  }
  impl_->outputRunning = false;
  if (impl_->outputThread.joinable()) impl_->outputThread.join();
  if (impl_->output) {
    (void)midiOutClose(impl_->output);
    impl_->output = nullptr;
  }
  impl_->lastQueued.reset();
}

bool MidiBridge::QueueFeedback(const LaunchpadLedFrame& frame) noexcept {
  if (!impl_->output) return false;
  if (impl_->lastQueued && *impl_->lastQueued == frame) return true;
  if (!impl_->feedbackQueue.TryPush(frame)) {
    ++impl_->droppedFeedback;
    return false;
  }
  impl_->lastQueued = frame;
  return true;
}

bool MidiBridge::IsOpen() const noexcept { return impl_->input != nullptr; }
bool MidiBridge::FeedbackEnabled() const noexcept { return impl_->output != nullptr; }
std::uint64_t MidiBridge::DroppedInput() const noexcept {
  return impl_->droppedInput.load();
}
std::uint64_t MidiBridge::DroppedFeedback() const noexcept {
  return impl_->droppedFeedback.load();
}
}  // namespace prismforge
