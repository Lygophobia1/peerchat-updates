#pragma once
#include "storage.hpp"
#include <memory>

namespace peerchat {
struct NodeEvent {
    enum class Kind { Ready, Contacts, TrustRequired, Connected, Disconnected, Message, History, Delivered, Error };
    Kind kind=Kind::Error;
    std::string peerId;
    std::wstring text;
    std::wstring detail;
    std::uint16_t port=0;
    std::vector<Contact> contacts;
    std::vector<StoredMessage> messages;
};
class Node {
public:
    Node();~Node();
    Node(const Node&)=delete;Node& operator=(const Node&)=delete;
    void start(const std::wstring& dataDirectory,std::uint16_t listeningPortOverride=0,const std::wstring& displayNameOverride={});
    void stop();
    void connect(const std::wstring& hostname,std::uint16_t port,const std::string& expectedIdentity={});
    void saveContact(const Contact& contact);
    void removeContact(const std::string& peerId);
    void approve(const std::string& peerId,bool accepted);
    std::string send(const std::string& peerId,const std::wstring& text);
    void loadHistory(const std::string& peerId,std::int64_t before=INT64_MAX);
    void setName(const std::wstring& name);
    std::vector<NodeEvent> poll();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
