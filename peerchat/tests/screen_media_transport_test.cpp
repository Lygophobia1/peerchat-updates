#include "screen_share_media.hpp"

#include <windows.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    HWND source = nullptr, viewer = nullptr;
    try {
        source = CreateWindowExW(0, L"STATIC", L"PeerChat H.264 sender test",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        viewer = CreateWindowExW(0, L"STATIC", L"PeerChat H.264 receiver test",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(source && viewer, "Could not create local screen-share test windows");
        ShowWindow(source, SW_SHOW);
        ShowWindow(viewer, SW_SHOW);
        UpdateWindow(source);
        UpdateWindow(viewer);
        peerchat::ScreenShareMedia::Options senderOptions;
        senderOptions.sender = true;
        senderOptions.sourceKind = PC_SCREEN_WINDOW;
        senderOptions.sourceHandle = reinterpret_cast<std::uint64_t>(source);
        senderOptions.width = 640;
        senderOptions.height = 480;
        senderOptions.fps = 60;
        senderOptions.bitrate = 4000000;
        peerchat::ScreenShareMedia::Options receiverOptions = senderOptions;
        receiverOptions.sender = false;
        receiverOptions.sourceHandle = 0;
        receiverOptions.presentationWindow = reinterpret_cast<std::uint64_t>(viewer);
        peerchat::ScreenShareMedia sender(senderOptions), receiver(receiverOptions);
        std::string error;
        require(sender.startOffer(error), error.c_str());
        bool answerStarted = false, senderConnected = false, receiverConnected = false;
        std::vector<std::pair<std::string, std::string>> pendingCandidates;
        unsigned updates = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        while (std::chrono::steady_clock::now() < deadline) {
            SetWindowTextW(source, (L"PeerChat H.264 sender test frame " +
                std::to_wstring(++updates)).c_str());
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            for (auto& event : sender.poll()) {
                if (event.kind == peerchat::ScreenShareMedia::EventKind::Description &&
                    event.detail == "offer") {
                    require(receiver.startAnswer(event.value, error), error.c_str());
                    answerStarted = true;
                    for (const auto& pending : pendingCandidates)
                        require(receiver.addCandidate(pending.first, pending.second),
                            "Receiver rejected a pending sender candidate");
                    pendingCandidates.clear();
                } else if (event.kind == peerchat::ScreenShareMedia::EventKind::Candidate) {
                    if (answerStarted)
                        require(receiver.addCandidate(event.value, event.detail),
                            "Receiver rejected a sender candidate");
                    else pendingCandidates.emplace_back(event.value, event.detail);
                } else if (event.kind == peerchat::ScreenShareMedia::EventKind::Connected)
                    senderConnected = true;
                else if (event.kind == peerchat::ScreenShareMedia::EventKind::Failed)
                    throw std::runtime_error("Sender secure media failed: " + event.value);
            }
            for (auto& event : receiver.poll()) {
                if (event.kind == peerchat::ScreenShareMedia::EventKind::Description &&
                    event.detail == "answer")
                    require(sender.setAnswer(event.value), "Sender rejected video answer");
                else if (event.kind == peerchat::ScreenShareMedia::EventKind::Candidate)
                    require(sender.addCandidate(event.value, event.detail),
                        "Sender rejected a receiver candidate");
                else if (event.kind == peerchat::ScreenShareMedia::EventKind::Connected)
                    receiverConnected = true;
                else if (event.kind == peerchat::ScreenShareMedia::EventKind::Failed)
                    throw std::runtime_error("Receiver secure media failed: " + event.value);
            }
            auto send = sender.statistics(), receive = receiver.statistics();
            if (senderConnected && receiverConnected && send.encodedSubmitted >= 16 &&
                receive.presented >= 1) {
                std::cout << "sender_encoded_submitted=" << send.encodedSubmitted
                          << " receiver_rtp=" << receive.rtpReceived
                          << " access_units=" << receive.accessUnitsDelivered
                          << " decoded=" << receive.decoded
                          << " presented=" << receive.presented << '\n';
                sender.stop();
                receiver.stop();
                DestroyWindow(source);
                DestroyWindow(viewer);
                std::cout << "PASS: H.264 window capture crossed local ICE/DTLS-SRTP and rendered.\n";
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        auto send = sender.statistics(), receive = receiver.statistics();
        std::cerr << "timeout sender_connected=" << senderConnected
                  << " receiver_connected=" << receiverConnected
                  << " encoded_submitted=" << send.encodedSubmitted
                  << " rtp_received=" << receive.rtpReceived
                  << " access_units=" << receive.accessUnitsDelivered
                  << " decoded=" << receive.decoded
                  << " presented=" << receive.presented << '\n';
        sender.stop();
        receiver.stop();
        DestroyWindow(source);
        DestroyWindow(viewer);
        return 1;
    } catch (const std::exception& failure) {
        if (source) DestroyWindow(source);
        if (viewer) DestroyWindow(viewer);
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
