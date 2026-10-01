#include "screen_share_media.hpp"
#include "screen_share_manager.hpp"

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

int main(int argc, char** argv) {
    const bool compatibility = argc == 2 &&
        (std::string(argv[1]) == "--compat" || std::string(argv[1]) == "--mixed");
    const bool mixed = argc == 2 && std::string(argv[1]) == "--mixed";
    if (argc > 2 || (argc == 2 && !compatibility)) return 2;
    HWND source = nullptr, viewer = nullptr, sourceContent = nullptr;
    try {
        peerchat::screen::ScreenShareManager share;
        const std::string firstId(32, 'a'), secondId(32, 'b'), peer(64, 'c');
        require(share.start(firstId, peer, true, peerchat::screen::State::Offering, 1000),
            "Could not create logical share");
        const auto firstLifetime = share.active()->lifetime;
        require(!share.start(secondId, peer, true, peerchat::screen::State::Offering, 1000),
            "Second simultaneous share was accepted");
        require(!share.matches(firstId, std::string(64, 'd')),
            "Other peer matched active share");
        require(share.matchesMedia(firstLifetime, firstId, peer, 1),
            "Initial media generation did not match");
        require(share.restart(2000) == 2 &&
            !share.matchesMedia(firstLifetime, firstId, peer, 1),
            "Stale media generation was not rejected");
        require(!share.acceptGeneration(2, 3000) &&
            !share.acceptGeneration(11, 3000) &&
            share.acceptGeneration(4, 3000) &&
            !share.acceptGeneration(3, 3000),
            "Replay, implausible generation jump or bounded catch-up was mishandled");
        require(share.expired(3000), "Recovery deadline was not enforced");
        share.clear();
        require(share.start(secondId, peer, false, peerchat::screen::State::Incoming, 3000)
            && !share.matchesMedia(firstLifetime, firstId, peer, 1),
            "Previous share callback survived logical teardown");
        share.clear();
        // A process must not carry peer/lifetime state from one share into the
        // next. Mirror the physical A->B, A->C, B->A, A->B sequence before
        // exercising the native media object below.
        const std::string peerB(64, 'b'), peerC(64, 'c'), peerA(64, 'a');
        const std::string shareAB(32, '1'), shareAC(32, '2'), shareBA(32, '3'), shareAB2(32, '4');
        require(share.start(shareAB, peerB, true, peerchat::screen::State::Offering, 4000),
            "Could not start A-to-B lifecycle");
        const auto abLifetime = share.active()->lifetime;
        share.clear();
        require(share.start(shareAC, peerC, true, peerchat::screen::State::Offering, 5000) &&
            !share.matchesMedia(abLifetime, shareAB, peerB, 1),
            "A-to-B state survived into A-to-C");
        share.clear();
        require(share.start(shareBA, peerA, true, peerchat::screen::State::Offering, 6000),
            "Could not start B-to-A lifecycle");
        share.clear();
        require(share.start(shareAB2, peerB, true, peerchat::screen::State::Offering, 7000),
            "Could not start repeated A-to-B lifecycle");
        share.clear();
        std::cerr << "logical_share_checks=passed\n";
        source = CreateWindowExW(0, L"STATIC", L"PeerChat H.264 sender test",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        viewer = CreateWindowExW(0, L"STATIC", L"PeerChat H.264 receiver test",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(source && viewer, "Could not create local screen-share test windows");
        sourceContent = CreateWindowExW(0, L"STATIC", L"Frame 0",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 30, 50, 500, 120, source,
            nullptr, GetModuleHandleW(nullptr), nullptr);
        require(sourceContent != nullptr, "Could not create animated capture content");
        std::cerr << "local_windows=created\n";
        ShowWindow(source, SW_SHOW);
        ShowWindow(viewer, SW_SHOW);
        SetWindowPos(source, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
        senderOptions.backend = compatibility ? peerchat::screen::Backend::WebRtcCompatibility :
            peerchat::screen::Backend::NativeWindows;
        senderOptions.ice = peerchat::MediaIceConfiguration::directForTests();
        peerchat::ScreenShareMedia::Options receiverOptions = senderOptions;
        receiverOptions.sender = false;
        if (mixed) receiverOptions.backend = peerchat::screen::Backend::NativeWindows;
        receiverOptions.sourceHandle = 0;
        receiverOptions.presentationWindow = reinterpret_cast<std::uint64_t>(viewer);
        peerchat::ScreenShareMedia sender(senderOptions), receiver(receiverOptions);
        std::string error;
        require(sender.startOffer(error), error.c_str());
        std::cerr << "video_offer=started\n";
        bool answerStarted = false, senderConnected = false, receiverConnected = false;
        std::vector<std::pair<std::string, std::string>> pendingCandidates;
        unsigned updates = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        while (std::chrono::steady_clock::now() < deadline) {
            SetWindowTextW(source, (L"PeerChat H.264 sender test frame " +
                std::to_wstring(++updates)).c_str());
            SetWindowTextW(sourceContent, (L"PeerChat live frame " +
                std::to_wstring(updates)).c_str());
            RedrawWindow(source, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
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
                require(sender.route()!=peerchat::ScreenShareMedia::Route::Unknown&&
                    receiver.route()!=peerchat::ScreenShareMedia::Route::Unknown,
                    "Connected screen media did not report its selected ICE route");
                std::cout << "sender_encoded_submitted=" << send.encodedSubmitted
                          << " receiver_rtp=" << receive.rtpReceived
                          << " access_units=" << receive.accessUnitsDelivered
                          << " decoded=" << receive.decoded
                          << " presented=" << receive.presented << '\n';
                sender.stop();
                receiver.stop();
                sender.stop();
                receiver.stop();
                require(!sender.statistics().connected && !receiver.statistics().connected &&
                    sender.poll().empty() && receiver.poll().empty(),
                    "Retired screen media revived after idempotent stop");
                peerchat::ScreenShareMedia cancelled(senderOptions);
                require(cancelled.startOffer(error), "Could not begin repeated share lifecycle");
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                cancelled.stop();
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
                  << " sender_track_open=" << send.trackOpen
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
