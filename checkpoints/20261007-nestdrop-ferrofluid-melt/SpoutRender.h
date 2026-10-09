#pragma once

#include <PrismForge/QualityGovernor.h>
#include <PrismForge/MusicalStateEngine.h>
#include <PrismForge/ShowState.h>
#include <PrismForge/SignalAnalyzer.h>

#include <filesystem>
#include <memory>
#include <string>

namespace prismforge {

// Owns one Direct3D 11 device and the PrismForge Spout sender. All methods must
// be called on the engine's render thread.
class SpoutRender {
 public:
  static constexpr unsigned kOutputWidth = 1920;
  static constexpr unsigned kOutputHeight = 1080;

  explicit SpoutRender(std::string senderName = "PrismForge");
  ~SpoutRender();
  SpoutRender(const SpoutRender&) = delete;
  SpoutRender& operator=(const SpoutRender&) = delete;

  bool Initialize(const std::filesystem::path& shaderDirectory, std::string& error);
  bool Render(const ShowSnapshot& show, const SignalFrameV1& signal,
              const MusicalStateFrameV1& musicalState,
              bool useMusicalRecursiveAudio, const QualityTier& quality,
              double seconds, std::string& error);
  // Clear only renderer-owned Recursive Circuit interpretation memory. The
  // caller uses this beside analyzer/MusicalStateEngine source resets.
  void ResetMusicalSceneState() noexcept;
  // Compiles every scene before replacing any live shader. On failure the last
  // valid set stays active.
  bool ReloadShaders(std::string& error);
  void Shutdown();
  [[nodiscard]] bool IsInitialized() const noexcept;
  [[nodiscard]] unsigned InternalWidth() const noexcept;
  [[nodiscard]] unsigned InternalHeight() const noexcept;

 private:
  struct Impl;
  std::string senderName_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace prismforge
