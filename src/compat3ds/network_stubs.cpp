// Definitions for SoH's networking and extraction classes, which this port does
// not build.
//
// These are C++ classes with virtual functions, so a link needs their vtables,
// not just their methods - which means defining them against the real headers
// rather than hand-declaring the symbols.
//
// The two groups behave differently on purpose:
//
//   Network / Anchor / CrowdControl / Sail
//     devkitPro packages no SDL2_net. Enable() reports and returns; the features
//     stay switched off and the UI simply never connects. Nothing here can
//     silently half-work, because the transport does not exist.
//
//   Extractor
//     Assets are extracted on the host (scripts + soh-torch). On-device
//     extraction would need torch cross-compiled and a ROM on the SD card, which
//     is a separate piece of work. GetRoms returns nothing, so the UI reports no
//     ROMs found rather than appearing to extract and producing a broken archive.

#include <cstdio>

#include "Network/Network.h"
#include "Network/Anchor/Anchor.h"
#include "Network/CrowdControl/CrowdControl.h"
#include "Network/Sail/Sail.h"
#include "Extractor/Extract.h"

namespace {
void ReportUnavailable(const char* feature) {
    std::fprintf(stderr, "soh-3ds: %s is unavailable (no SDL2_net for 3DS)\n", feature);
}
} // namespace

// --- Network base ----------------------------------------------------------

void Network::OnIncomingData(char[512]) {
}
void Network::OnIncomingJson(nlohmann::json) {
}
void Network::OnConnected() {
}
void Network::OnDisconnected() {
}
void Network::ProcessOutgoingPackets() {
}
void Network::SendJsonToRemote(nlohmann::json) {
}

// --- Anchor ----------------------------------------------------------------

// A vtable needs every virtual defined, not just the ones the port calls -
// these three classes each override part of Network's interface.
void Anchor::OnIncomingJson(nlohmann::json) {
}
void Anchor::OnConnected() {
}
void Anchor::OnDisconnected() {
}
void Anchor::ProcessOutgoingPackets() {
}
void Anchor::SendJsonToRemote(nlohmann::json) {
}

void Anchor::Enable() {
    ReportUnavailable("Anchor (multiplayer sync)");
}
void Anchor::Disable() {
}

void AnchorRoomWindow::DrawElement() {
}
void AnchorRoomWindow::Draw() {
}

// --- CrowdControl ----------------------------------------------------------

void CrowdControl::OnIncomingJson(nlohmann::json) {
}
void CrowdControl::OnConnected() {
}
void CrowdControl::OnDisconnected() {
}

void CrowdControl::Enable() {
    ReportUnavailable("Crowd Control");
}
void CrowdControl::Disable() {
}

// --- Sail ------------------------------------------------------------------

void Sail::OnIncomingJson(nlohmann::json) {
}
void Sail::OnConnected() {
}
void Sail::OnDisconnected() {
}

void Sail::Enable() {
    ReportUnavailable("Sail");
}
void Sail::Disable() {
}

// --- Extractor -------------------------------------------------------------

void Extractor::GetRoms(std::vector<std::string>& roms) {
    roms.clear();
}

bool Extractor::IsMasterQuest() const {
    return false;
}

void Extractor::SetSearchPath(const std::string&) {
}

bool Extractor::ManuallySearchForRomMatchingType(RomSearchMode) {
    return false;
}

bool Extractor::RunFileStandalone(std::string) {
    return false;
}

bool Extractor::CallTorch(std::string, std::string, std::atomic<size_t>*, std::atomic<size_t>*) {
    std::fprintf(stderr, "soh-3ds: on-device extraction is not built; extract oot.o2r on a PC\n");
    return false;
}

void Extractor::ShowErrorBox(const char* title, const char* text) {
    std::fprintf(stderr, "soh-3ds: %s: %s\n", title, text);
}

// --- Speech synthesis ------------------------------------------------------
// SAPI is Windows-only and ESpeak is an optional desktop dependency, so
// accessibility narration is genuinely absent here. SpeechLogger is the
// fallback "synthesizer" that only logs, so it is the one SoH constructs;
// DoInit reporting false leaves narration switched off rather than pretending.

#include "Enhancements/speechsynthesizer/SpeechSynthesizer.h"

bool SpeechSynthesizer::Init(void) {
    return false;
}

// SpeechSynthesizer's constructor is declared but its definition lives in the
// excluded SpeechSynthesizer.cpp, and SpeechLogger's base initialiser needs it.
SpeechSynthesizer::SpeechSynthesizer() = default;

SpeechLogger::SpeechLogger() = default;

void SpeechLogger::Speak(const char*, const char*) {
}

bool SpeechLogger::DoInit(void) {
    return false;
}

void SpeechLogger::DoUninitialize(void) {
}
