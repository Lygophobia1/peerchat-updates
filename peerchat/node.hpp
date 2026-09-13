#pragma once
#include "storage.hpp"
#include "group.hpp"
#include "media_telemetry.hpp"
#include <memory>

namespace peerchat {
struct NodeEvent {
    enum class Kind { Ready, Contacts, TrustRequired, Connected, Disconnected, Message, History, Delivered, Error, Groups, GroupMessage, GroupHistory, GroupDelivered, VoiceCall, ConnectionAttempt, ReconnectScheduled, ConnectionLifecycle, SessionSnapshot };
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
    std::uint64_t sessionEpoch=0;
    std::uint64_t sessionRevision=0;
    std::uint64_t networkGeneration=0;
    bool sessionConnected=false;
    std::uint32_t retryDelayMilliseconds=0;
    std::vector<Contact> contacts;
    std::vector<StoredMessage> messages;
    std::vector<GroupState> groups;
    std::vector<GroupEvent> groupEvents;
};
class Node {
public:
    Node();~Node();
    Node(const Node&)=delete;Node& operator=(const Node&)=delete;
    void start(const std::wstring& dataDirectory,std::uint16_t listeningPortOverride=0,const std::wstring& displayNameOverride={});
    void stop();
    void connect(const std::wstring& hostname,std::uint16_t port,const std::string& expectedIdentity={},const std::wstring& candidateSource=L"Direct",const std::wstring& route=L"manual",bool forceImmediate=false);
    void invalidateRoute(const std::string& expectedIdentity,const std::wstring& staleHost={},std::uint16_t stalePort=0,bool allRoutes=false,std::uint64_t expectedNetworkGeneration=0);
    void setNetworkContext(std::uint64_t generation,const std::wstring& fingerprint);
    void saveContact(const Contact& contact);
    void removeContact(const std::string& peerId);
    void approve(const std::string& peerId,bool accepted);
    std::string send(const std::string& peerId,const std::wstring& text);
    void loadHistory(const std::string& peerId,std::int64_t before=INT64_MAX);
    void setName(const std::wstring& name);
    void setGroupAccount(std::uint64_t accountId,const std::wstring& accountName);
    std::string createGroup(const std::wstring& name);
    bool renameGroup(const std::string& groupId,const std::wstring& name);
    bool inviteToGroup(const std::string& groupId,std::uint64_t accountId,const std::string& peerId,const std::wstring& name);
    bool respondToGroupInvite(const std::string& groupId,bool accept);
    bool leaveGroup(const std::string& groupId);
    bool removeGroupMember(const std::string& groupId,std::uint64_t accountId);
    bool setGroupAdmin(const std::string& groupId,std::uint64_t accountId,bool admin);
    bool sendGroup(const std::string& groupId,const std::wstring& text);
    void loadGroups();
    void loadGroupHistory(const std::string& groupId,std::int64_t before=INT64_MAX);
    std::string startVoiceCall(const std::string& peerId);
    bool answerVoiceCall(const std::string& callId);
    bool declineVoiceCall(const std::string& callId);
    bool endVoiceCall(const std::string& callId);
    bool setVoiceMuted(bool muted);
    bool setVoiceDeafened(bool deafened);
    bool setVoiceDevices(int inputDevice, int outputDevice);
    std::wstring voiceDiagnostics();
    MediaTelemetry voiceTelemetry() const;
    std::vector<NodeEvent> poll();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
