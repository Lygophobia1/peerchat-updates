#pragma once
#include "storage.hpp"
#include "group.hpp"
#include "media_telemetry.hpp"
#include "media_ice_configuration.hpp"
#include "group_key_manager.hpp"
#include "screen_share_manager.hpp"
#include <memory>
#include <set>

namespace peerchat {
// A profile-scoped projection of the authenticated account/friend snapshot.
// It authorizes only the narrow initial-device TOFU transition below; it is
// never a substitute for the peer TLS certificate or Peer ID check.
struct AuthenticatedFriend {
    std::uint64_t accountId = 0;
    std::string expectedPeerId;
    std::vector<std::string> knownPeerIds;
};
struct NodeEvent {
    enum class Kind { Ready, Contacts, TrustRequired, Connected, Disconnected, Message, History, Delivered, Error, Groups, GroupMessage, GroupHistory, GroupDelivered, VoiceCall, ConnectionAttempt, ReconnectScheduled, ConnectionLifecycle, SessionSnapshot, RelayRequested, ScreenShare, GroupCiphertext, GroupCiphertextAck, FileTransfer };
    Kind kind=Kind::Error;
    std::string peerId;
    std::string expectedPeerId;
    std::wstring text;
    std::wstring detail;
    std::wstring candidate;
    std::wstring candidateSource;
    std::wstring route;
    std::uint16_t port=0;
    std::uint64_t attemptId=0;
    std::string authorizationGroupId;
    std::wstring authorizationArchive;
    std::uint64_t authorizationRosterGeneration=0;
    std::uint64_t authorizationTargetAccountId=0;
    std::uint64_t sessionEpoch=0;
    std::uint64_t sessionRevision=0;
    std::uint64_t networkGeneration=0;
    bool sessionConnected=false;
    std::uint64_t callLifetime=0;
    std::uint64_t shareLifetime=0;
    std::uint64_t shareGeneration=0;
    std::uint32_t shareState=0;
    std::uint32_t screenRoute=0,screenWidth=0,screenHeight=0,screenFps=0;
    double screenBitrateMbps=0;
    std::wstring screenSummary,screenDetails;
    // Authoritative screen-media classification. These are intentionally
    // separate from the user-visible route and never contain credentials.
    std::wstring screenFailureDomain,screenFailureReason,screenRecoveryTrigger;
    std::uint64_t screenRecoveryGenerationFrom=0,screenRecoveryGenerationTo=0;
    std::uint32_t callState=0;
    std::uint32_t callType=0;
    std::uint32_t mediaState=0;
    std::uint64_t mediaGeneration=0;
    bool callMuted=false;
    bool callDeafened=false;
    bool callControlAvailable=true;
    bool screenMediaFailed=false;
    std::uint32_t reliableRoute=0;
    // The manager owns this value; WPF uses it only to present the optional
    // verified-direct standby/promotion controls.
    std::uint32_t p2pAvailability=0;
    std::uint32_t mediaRoute=0;
    std::uint32_t retryDelayMilliseconds=0;
    std::vector<Contact> contacts;
    std::vector<StoredMessage> messages;
    // Native-verified account projection only. Original sender IDs stay intact.
    std::set<std::string> localMessageAuthors;
    std::vector<GroupState> groups;
    std::vector<GroupEvent> groupEvents;
};
class Node {
public:
    Node();~Node();
    Node(const Node&)=delete;Node& operator=(const Node&)=delete;
    // Production profiles start relay-only. Test fixtures and the retained
    // explicit Direct Only path may opt into a listener.
    void start(const std::wstring& dataDirectory,std::uint16_t listeningPortOverride=0,const std::wstring& displayNameOverride={},bool startDirectListener=true);
    void stop();
    void connect(const std::wstring& hostname,std::uint16_t port,const std::string& expectedIdentity={},const std::wstring& candidateSource=L"Direct",const std::wstring& route=L"manual",bool forceImmediate=false);
    void invalidateRoute(const std::string& expectedIdentity,const std::wstring& staleHost={},std::uint16_t stalePort=0,bool allRoutes=false,std::uint64_t expectedNetworkGeneration=0);
    void setNetworkContext(std::uint64_t generation,const std::wstring& fingerprint);
    void updateCandidates(const std::string& peerId,const std::wstring& lanHost,std::uint16_t lanPort,
                          const std::wstring& publicHost,std::uint16_t publicPort,bool relayAvailable,
                          std::uint64_t networkGeneration,bool forceImmediate=false);
    // Narrow privacy-safe boundary for a discovery-authorized temporary route.
    // It is intentionally not a generic direct-connect API.
    bool beginScopedP2pProbe(const std::string& peerId,const std::string& authorizationId,
                             const std::wstring& host,std::uint16_t port,
                             std::uint64_t authorizationExpiryUtcSeconds,std::uint64_t networkGeneration);
    bool prepareScopedP2pListener(const std::string& peerId);
    bool promoteVerifiedP2P(const std::string& peerId,const std::string& authorizationId);
    void cancelScopedP2p(const std::string& peerId);
    void replaceAuthenticatedFriends(std::uint64_t localAccountId,std::uint64_t snapshotGeneration,
                                     std::vector<AuthenticatedFriend> friends);
    bool pinInitialAuthenticatedFriendIdentity(const std::string& peerId,std::uint64_t attemptId,
                                              std::uint64_t friendSnapshotGeneration);
    bool relayReady(std::uint64_t attemptId,bool incoming);
    bool relayReceive(std::uint64_t attemptId,const unsigned char* bytes,std::size_t length);
    void relayClosed(std::uint64_t attemptId,const std::wstring& reason);
    int takeRelayOutput(std::uint64_t attemptId,unsigned char* bytes,std::size_t capacity,std::uint32_t waitMilliseconds);
    void saveContact(const Contact& contact);
    void removeContact(const std::string& peerId);
    void approve(const std::string& peerId,bool accepted);
    std::string send(const std::string& peerId,const std::wstring& text);
    std::wstring fileTransfer(unsigned operation,const std::string& peer={},const std::string& id={},const std::wstring& path={});
    void loadHistory(const std::string& peerId,std::int64_t before=INT64_MAX);
    bool eraseHistory(const std::string& peerId);
    void setName(const std::wstring& name);
    void setGroupAccount(std::uint64_t accountId,const std::wstring& accountName);
    bool setAuthenticatedGroupRoster(std::uint64_t localAccountId,std::uint64_t discoveryGeneration,group_keys::RosterSnapshot roster);
    void clearAuthenticatedGroupRosters();
    bool beginGroupControl(const std::string& serverGroupId,std::uint64_t targetAccountId,const std::string& targetPeerId,std::uint64_t discoveryGeneration);
    std::string createGroup(const std::wstring& name);
    // Returns only the already-signed content-free Create event for an owner
    // migration claim. It never exports history or group key material.
    std::wstring groupCreationProof(const std::string& groupId);
    bool renameGroup(const std::string& groupId,const std::wstring& name);
    bool inviteToGroup(const std::string& groupId,std::uint64_t accountId,const std::string& peerId,const std::wstring& name);
    bool respondToGroupInvite(const std::string& groupId,bool accept);
    bool leaveGroup(const std::string& groupId);
    bool removeGroupMember(const std::string& groupId,std::uint64_t accountId);
    bool setGroupAdmin(const std::string& groupId,std::uint64_t accountId,bool admin);
    bool sendGroup(const std::string& groupId,const std::wstring& text);
    void loadGroups();
    void loadGroupHistory(const std::string& groupId,std::int64_t before=INT64_MAX);
    bool receiveGroupCiphertext(const Blob& envelope);
    bool groupMessageStatus(const std::string& group,const std::string& message,const std::wstring& status,std::uint32_t stage=0);
    std::string startVoiceCall(const std::string& peerId);
    bool answerVoiceCall(const std::string& callId);
    bool declineVoiceCall(const std::string& callId);
    bool endVoiceCall(const std::string& callId);
    bool setVoiceMuted(bool muted);
    bool setVoiceDeafened(bool deafened);
    bool setVoiceDevices(int inputDevice, int outputDevice);
    bool setVoiceVolumes(int inputPercent, int outputPercent);
    bool setVoiceProcessing(bool enabled, bool noiseSuppression, bool automaticGain,
                            std::uint32_t noiseSuppressionStrength,
                            std::uint32_t automaticGainStrength, bool echoCancellation=false);
    bool setMediaIceConfiguration(MediaIceConfiguration configuration);
    bool setScreenShareBackend(screen::Backend backend);
    std::wstring voiceDiagnostics();
    std::string startScreenShare(const std::string& peerId,std::uint32_t sourceKind,std::uint64_t sourceHandle,std::uint32_t fps,std::uint32_t bitrate,bool captureCursor,screen::Backend backend=screen::Backend::NativeWindows);
    bool answerScreenShare(const std::string& shareId,std::uint64_t presentationWindow,screen::Backend backend=screen::Backend::NativeWindows);
    bool declineScreenShare(const std::string& shareId);
    bool endScreenShare(const std::string& shareId);
    std::wstring screenShareDiagnostics();
    MediaTelemetry voiceTelemetry() const;
    std::vector<NodeEvent> poll();
    Blob mailbox(unsigned operation,const Blob& input={},const std::string& messageId={},bool storageAllowed=false);
    Blob archive(unsigned operation,const Blob& input);
    std::wstring groupAttachment(unsigned operation,const std::string& group,const std::string& id,const std::wstring& path);
    Blob groupAttachmentNetwork(unsigned operation,const Blob& input);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
