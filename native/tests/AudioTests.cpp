#include "AudioCapture.h"
#include "AudioSwitcher.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace prismforge;
using namespace std::chrono_literals;

namespace {

void Check(bool condition, const char* expression) {
  if (!condition) throw std::runtime_error(expression);
}
#define CHECK(expression) Check(static_cast<bool>(expression), #expression)

struct FakeState {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> log;
  std::string failId;
  std::string blockedId;
  bool release = false;
  std::atomic_bool enteredBlockedStart{false};
  std::atomic_bool interrupted{false};
};

class FakeCapture final : public IAudioCapture {
 public:
  explicit FakeCapture(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}

  bool Start(bool /*loopback*/, const std::string& sourceId,
             std::string& error) override {
    id_ = sourceId.empty() ? "system-default" : sourceId;
    {
      std::unique_lock lock(state_->mutex);
      CHECK(!active_);  // Candidate callbacks must remain gated during start.
      state_->log.push_back("start:" + id_);
      if (id_ == state_->blockedId) {
        state_->enteredBlockedStart = true;
        state_->cv.wait(lock, [&] { return state_->release; });
      }
      if (id_ == state_->failId) {
        error = "synthetic open failure";
        return false;
      }
    }
    running_ = true;
    return true;
  }

  void Stop() override {
    if (!running_) return;
    running_ = false;
    std::lock_guard lock(state_->mutex);
    state_->log.push_back("stop:" + id_);
  }

  std::vector<AudioDeviceInfo> Devices(std::string& error) override {
    error.clear();
    return {{WasapiSourceId(false, L"A"), "A", false},
            {WasapiSourceId(false, L"B"), "B", false},
            {WasapiSourceId(false, L"C"), "C", false}};
  }

  void SetActive(bool active) noexcept override {
    active_ = active;
    std::lock_guard lock(state_->mutex);
    state_->log.push_back(std::string(active ? "on:" : "off:") + id_);
  }

  bool IsRunning() const noexcept override {
    return running_ && !state_->interrupted.load();
  }
  unsigned SampleRate() const noexcept override { return 48000; }

 private:
  std::shared_ptr<FakeState> state_;
  std::string id_;
  bool active_ = false;
  bool running_ = false;
};

AudioSwitchEvent WaitFor(AudioSwitcher& switcher, AudioSwitchEventKind kind) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto event = switcher.Poll()) {
      if (event->kind == kind) return *event;
    }
    std::this_thread::sleep_for(1ms);
  }
  throw std::runtime_error("audio worker event timed out");
}

void TestStableEndpointIdentity() {
  CHECK(WasapiSourceId(false, L"A") == "input:wasapi:0041");
  CHECK(WasapiSourceId(true, L"A") == "loopback:wasapi:0041");
  CHECK(WasapiSourceId(false, std::wstring_view(L"A\0B", 3)).empty());
  CHECK(WasapiSourceId(false, L"").empty());
}

void TestCallbackDrainBarrier() {
  detail::AudioCallbackGate gate(true);
  CHECK(gate.Enter());  // An old callback has passed the active gate.
  std::atomic_bool drained{false};
  std::thread worker([&] {
    gate.DeactivateAndDrain();
    drained.store(true);
  });
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (gate.Active() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  const bool deactivated = !gate.Active();
  const bool prematureDrain = drained.load();
  const bool lateCallbackEntered = gate.Enter();
  if (lateCallbackEntered) gate.Exit();
  gate.Exit();  // The old callback has completed all possible queue writes.
  worker.join();
  CHECK(deactivated);
  CHECK(!prematureDrain);
  CHECK(!lateCallbackEntered);
  CHECK(drained.load());
  CHECK(!gate.Enter());
  gate.Activate();
  CHECK(gate.Enter());
  gate.Exit();
}

void TestFailedSwitchRetainsOldAndDoesNotBlockCaller() {
  BoundedQueue<AudioBlock> samples(8);
  auto state = std::make_shared<FakeState>();
  const auto sourceA = WasapiSourceId(false, L"A");
  const auto sourceB = WasapiSourceId(false, L"B");
  state->failId = sourceB;
  AudioSwitcher switcher(samples, [state](BoundedQueue<AudioBlock>&) {
    return std::make_unique<FakeCapture>(state);
  });
  std::string error;
  CHECK(switcher.Start(false, error));
  const auto sources = WaitFor(switcher, AudioSwitchEventKind::Sources);
  CHECK(sources.hasDevices && sources.devices.size() == 3);
  CHECK(!sources.connected && !switcher.IsRunning());
  CHECK(!switcher.Request("input:0", error));
  CHECK(!switcher.Request("A", error));
  CHECK(switcher.Request(sourceA, error));
  const auto first = WaitFor(switcher, AudioSwitchEventKind::Switched);
  CHECK(first.activeId == sourceA && first.connected && switcher.IsRunning());
  CHECK(switcher.Request(sourceB, error));
  const auto failed = WaitFor(switcher, AudioSwitchEventKind::Failed);
  CHECK(failed.requestedId == sourceB && failed.activeId == sourceA);
  CHECK(failed.error == "synthetic open failure" && failed.connected);
  CHECK(switcher.IsRunning());
  {
    std::lock_guard lock(state->mutex);
    for (const auto& item : state->log) CHECK(item != "stop:" + sourceA);
  }
  switcher.Stop();
}

void TestSwitchHandoffAndBoundedRequests() {
  BoundedQueue<AudioBlock> samples(8);
  auto state = std::make_shared<FakeState>();
  const auto sourceA = WasapiSourceId(false, L"A");
  const auto sourceB = WasapiSourceId(false, L"B");
  state->blockedId = sourceB;
  AudioSwitcher switcher(samples, [state](BoundedQueue<AudioBlock>&) {
    return std::make_unique<FakeCapture>(state);
  });
  std::string error;
  CHECK(switcher.Start(false, error));
  (void)WaitFor(switcher, AudioSwitchEventKind::Sources);
  CHECK(switcher.Request(sourceA, error));
  (void)WaitFor(switcher, AudioSwitchEventKind::Switched);
  CHECK(switcher.Request(sourceB, error));
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!state->enteredBlockedStart && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  CHECK(state->enteredBlockedStart);
  // The worker is stuck in a synthetic replacement open. The caller can still
  // submit exactly the four bounded pending requests without waiting for it.
  for (int i = 0; i < 4; ++i) CHECK(switcher.Request(sourceA, error));
  CHECK(!switcher.Request(sourceA, error));
  CHECK(error == "Audio switch queue is full");
  CHECK(switcher.IsRunning());
  {
    std::lock_guard lock(state->mutex);
    state->release = true;
  }
  state->cv.notify_all();
  const auto switched = WaitFor(switcher, AudioSwitchEventKind::Switched);
  CHECK(switched.activeId == sourceB);
  switcher.Stop();
  {
    std::lock_guard lock(state->mutex);
    auto offA = std::find(state->log.begin(), state->log.end(), "off:" + sourceA);
    auto onB = std::find(state->log.begin(), state->log.end(), "on:" + sourceB);
    auto stopA = std::find(state->log.begin(), state->log.end(), "stop:" + sourceA);
    CHECK(offA != state->log.end() && onB != state->log.end() && stopA != state->log.end());
    CHECK(offA < onB && onB < stopA);
  }
}

void TestInterruptedCaptureRecoversOnce() {
  BoundedQueue<AudioBlock> samples(8);
  auto state = std::make_shared<FakeState>();
  const auto sourceA = WasapiSourceId(false, L"A");
  AudioSwitcher switcher(samples, [state](BoundedQueue<AudioBlock>&) {
    return std::make_unique<FakeCapture>(state);
  });
  std::string error;
  CHECK(switcher.Start(false, error));
  (void)WaitFor(switcher, AudioSwitchEventKind::Sources);
  CHECK(switcher.Request(sourceA, error));
  (void)WaitFor(switcher, AudioSwitchEventKind::Switched);
  CHECK(switcher.IsRunning());

  state->interrupted.store(true);
  const auto lost = WaitFor(switcher, AudioSwitchEventKind::Disconnected);
  CHECK(lost.activeId == sourceA && !lost.connected);
  CHECK(!switcher.IsRunning());

  state->interrupted.store(false);
  const auto recovered = WaitFor(switcher, AudioSwitchEventKind::Switched);
  CHECK(recovered.requestedId == sourceA && recovered.activeId == sourceA);
  CHECK(recovered.connected && switcher.IsRunning());
  std::this_thread::sleep_for(30ms);
  CHECK(!switcher.Poll());  // No duplicate recovery on later worker polls.
  switcher.Stop();
}

}  // namespace

int main() {
  TestStableEndpointIdentity();
  TestCallbackDrainBarrier();
  TestFailedSwitchRetainsOldAndDoesNotBlockCaller();
  TestSwitchHandoffAndBoundedRequests();
  TestInterruptedCaptureRecoversOnce();
}
