using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Windows;
using System.Windows.Media;

namespace PeerChatDesktop {
public abstract class ObservableModel : INotifyPropertyChanged {
    public event PropertyChangedEventHandler PropertyChanged;
    protected void Changed(string name){if(PropertyChanged!=null)PropertyChanged(this,new PropertyChangedEventArgs(name));}
}
public class ConversationModel : ObservableModel {
    public ContactData Contact;
    public string ConversationId="";
    public long AccountId;
    public string AccountName="";
    public bool IsFriend;
    public string PreviousVerifiedPeerId="";
    public readonly HashSet<string> KnownPeerIds=new HashSet<string>(StringComparer.Ordinal);
    public readonly HashSet<string> RequestedPeerHistory=new HashSet<string>(StringComparer.Ordinal);
    public readonly ObservableCollection<MessageModel> Messages=new ObservableCollection<MessageModel>();
    public string Draft="";
    public bool Online,RequestedHistory;
    public bool PublicAttempted;
    // Presentation state mirrors the core's connection lifecycle. It is deliberately
    // separate from account/friend state so a direct P2P chat can work offline.
    public string ConnectionState="Offline";
    public int Unread;
    public string Name {get{return !String.IsNullOrEmpty(AccountName)?AccountName:Contact.name;}}
    public string Initial {get{return String.IsNullOrEmpty(Name)?"?":System.Globalization.StringInfo.GetNextTextElement(Name).ToUpperInvariant();}}
    public Brush Presence {get{return new SolidColorBrush(Online?Color.FromRgb(54,216,115):Color.FromRgb(107,121,135));}}
    public string Preview {get{return Messages.Count>0?Messages.Last().Text:(Contact.verified?"Start a conversation":"Awaiting verification");}}
    public string When {get{return Messages.Count>0?Messages.Last().ShortTime:"";}}
    public string UnreadLabel {get{return Unread>0?Unread+" unread":"";}}
    public void Refresh(){foreach(string name in new[]{"Name","Initial","Presence","Preview","When","UnreadLabel","ConnectionState"})Changed(name);}
}
public class GroupModel : ObservableModel {
    public GroupData Data;
    public readonly ObservableCollection<MessageModel> Messages=new ObservableCollection<MessageModel>();
    public string Draft="";
    public int Unread;
    public bool Invited;
    public string Id {get{return Data.id;}}
    public string Name {get{return Data.name;}}
    public string Initial {get{return String.IsNullOrEmpty(Name)?"G":System.Globalization.StringInfo.GetNextTextElement(Name).ToUpperInvariant();}}
    public Brush Presence {get{return new SolidColorBrush(Color.FromRgb(66,151,232));}}
    public string Preview {get{return Messages.Count>0?Messages.Last().Text:(Invited?"Invitation · Accept or decline":MemberCount+" members");}}
    public string When {get{return Messages.Count>0?Messages.Last().ShortTime:"";}}
    public string UnreadLabel {get{return Unread>0?Unread+" unread":"";}}
    public int MemberCount {get{return Data.members==null?0:Data.members.Length;}}
    public void Refresh(){foreach(string name in new[]{"Name","Initial","Presence","Preview","When","UnreadLabel","MemberCount"})Changed(name);}
}
public class MessageModel : ObservableModel {
    public MessageData Data;
    public bool Outgoing;
    double bubbleWidth=400;
    public double BubbleWidth {get{return bubbleWidth;}set{bubbleWidth=value;Changed("BubbleWidth");}}
    public string Text {get{return Data.text;}}
    public HorizontalAlignment Alignment {get{return Outgoing?HorizontalAlignment.Right:HorizontalAlignment.Left;}}
    public Brush Bubble {get{return new SolidColorBrush(Outgoing?Color.FromRgb(27,73,125):Color.FromRgb(31,42,52));}}
    public DateTime LocalTime {get{try{return new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddMilliseconds(Data.time).ToLocalTime();}catch{return DateTime.MinValue;}}}
    public string ShortTime {get{return LocalTime.Date==DateTime.Today?LocalTime.ToString("h:mm tt"):LocalTime.ToString("MMM d");}}
    public string Stamp {get{return LocalTime.ToString("MMM d · h:mm tt")+(Outgoing?(Data.delivery==2?"  ✓✓ Delivered":"  ◷ Queued"):"");}}
    public void Delivered(){Data.delivery=2;Changed("Stamp");}
}
// Dispatcher-confined presentation state. Core events, never the GUI, determine
// online/trust/delivery state. Switching views retains drafts and history.
public class ChatViewModel {
    public readonly Dictionary<string,ConversationModel> Conversations=new Dictionary<string,ConversationModel>();
    readonly Dictionary<string,ConversationModel> conversationsByPeer=new Dictionary<string,ConversationModel>(StringComparer.Ordinal);
    public readonly Dictionary<string,GroupModel> GroupChats=new Dictionary<string,GroupModel>();
    public string OwnId="",Name="You",Status="Opening your encrypted profile…";
    public ushort Port=7777;
    public bool Listening,Groups;
    public ConversationModel Selected;
    public GroupModel SelectedGroup;
    public ConversationModel Get(string peer){ConversationModel result;if(!String.IsNullOrEmpty(peer)&&conversationsByPeer.TryGetValue(peer,out result))return result;string key="peer:"+(peer??"");result=new ConversationModel{ConversationId=key,Contact=new ContactData{peer=peer,name="Friend",host=""}};if(!String.IsNullOrEmpty(peer)){result.KnownPeerIds.Add(peer);conversationsByPeer[peer]=result;}Conversations[key]=result;return result;}
    public ConversationModel FindByPeer(string peer){ConversationModel result;return !String.IsNullOrEmpty(peer)&&conversationsByPeer.TryGetValue(peer,out result)?result:null;}
    public ConversationModel BindFriend(long accountId,string username,string currentPeer,IEnumerable<string> knownPeers,out bool reused){
        if(accountId<=0)throw new ArgumentOutOfRangeException("accountId");string key="account:"+accountId;ConversationModel conversation;reused=Conversations.TryGetValue(key,out conversation);
        ConversationModel peerConversation=null;if(!String.IsNullOrEmpty(currentPeer))conversationsByPeer.TryGetValue(currentPeer,out peerConversation);
        if(!reused&&peerConversation!=null){conversation=peerConversation;Conversations.Remove(conversation.ConversationId);reused=true;}
        if(!reused){conversation=new ConversationModel{Contact=new ContactData{peer=currentPeer,name=username,host=""}};}
        if(peerConversation!=null&&peerConversation!=conversation){foreach(var message in peerConversation.Messages)if(!conversation.Messages.Any(existing=>existing.Data.id==message.Data.id))conversation.Messages.Add(message);foreach(var peer in peerConversation.KnownPeerIds){conversation.KnownPeerIds.Add(peer);conversationsByPeer[peer]=conversation;}if(String.IsNullOrEmpty(conversation.Draft))conversation.Draft=peerConversation.Draft;conversation.Unread+=peerConversation.Unread;Conversations.Remove(peerConversation.ConversationId);}
        conversation.ConversationId=key;conversation.AccountId=accountId;conversation.AccountName=username??"";conversation.IsFriend=true;Conversations[key]=conversation;
        if(knownPeers!=null)foreach(var peer in knownPeers)if(!String.IsNullOrEmpty(peer)){conversation.KnownPeerIds.Add(peer);conversationsByPeer[peer]=conversation;}
        if(conversation.Contact==null)conversation.Contact=new ContactData{peer=currentPeer,name=username,host=""};
        string previous=conversation.Contact.peer??"";bool changed=!String.IsNullOrEmpty(previous)&&!String.IsNullOrEmpty(currentPeer)&&previous!=currentPeer;
        if(changed&&conversation.Contact.verified)conversation.PreviousVerifiedPeerId=previous;
        if(!String.IsNullOrEmpty(currentPeer)){conversation.KnownPeerIds.Add(currentPeer);conversationsByPeer[currentPeer]=conversation;if(changed)conversation.Contact=new ContactData{peer=currentPeer,name=username,host="",verified=false};else{conversation.Contact.peer=currentPeer;conversation.Contact.name=username;}}
        conversation.Refresh();return conversation;
    }
    public void ReconcileFriends(ISet<long> activeAccounts){foreach(var conversation in Conversations.Values)if(conversation.AccountId>0)conversation.IsFriend=activeAccounts.Contains(conversation.AccountId);}
    public void RemoveConversation(ConversationModel conversation){if(conversation==null)return;Conversations.Remove(conversation.ConversationId);foreach(var peer in conversation.KnownPeerIds.ToArray())if(conversationsByPeer.ContainsKey(peer)&&conversationsByPeer[peer]==conversation)conversationsByPeer.Remove(peer);}
    static void OrderMessages(ObservableCollection<MessageModel> messages){
        var ordered=messages.OrderBy(m=>m.Data.logical).ThenBy(m=>m.Data.sender,StringComparer.Ordinal).ThenBy(m=>m.Data.id,StringComparer.Ordinal).ToList();
        for(int index=0;index<ordered.Count;index++){int current=messages.IndexOf(ordered[index]);if(current!=index)messages.Move(current,index);}
    }
    public void Apply(CoreEvent e){
        if(e.kind==0){OwnId=e.peer;Name=e.text;Port=e.port;Listening=true;Status="Ready to connect";}
        if(e.kind==1&&e.contacts!=null)foreach(var contact in e.contacts){var item=Get(contact.peer);item.KnownPeerIds.Add(contact.peer);if(item.AccountId==0||item.Contact==null||item.Contact.peer==contact.peer){string accountName=item.AccountName;item.Contact=contact;if(!String.IsNullOrEmpty(accountName))item.Contact.name=accountName;if(contact.verified)item.PreviousVerifiedPeerId="";}item.Refresh();}
        if(e.kind==3){var item=Get(e.peer);item.Online=true;item.ConnectionState="Ready";item.Refresh();Status=e.text+" connected";}
        if(e.kind==4){var item=Get(e.peer);item.Online=false;item.ConnectionState="Retrying";item.Refresh();Status="Peer disconnected. Reconnect is available.";}
        if(e.kind==2){var item=Get(e.peer);if(e.contacts!=null&&e.contacts.Length>0)item.Contact=e.contacts[0];item.ConnectionState="Awaiting peer verification";item.Online=false;item.Refresh();}
        if(e.kind==9)Status=e.text;
        if((e.kind==5||e.kind==6)&&e.messages!=null){var item=Get(e.peer);foreach(var message in e.messages){if(item.Messages.Any(m=>m.Data.id==message.id))continue;var model=new MessageModel{Data=message,Outgoing=message.sender==OwnId};item.Messages.Add(model);if(e.kind==5&&!model.Outgoing&&(Selected!=item||Groups))item.Unread++;}OrderMessages(item.Messages);item.Refresh();}
        // NodeEvent::Delivered is 7 and Error is 8 (the enum is the ABI contract).
        if(e.kind==7&&e.messages!=null)foreach(var item in Conversations.Values)foreach(var receipt in e.messages){var match=item.Messages.FirstOrDefault(m=>m.Data.id==receipt.id);if(match!=null)match.Delivered();}
        if(e.kind==8){var item=String.IsNullOrEmpty(e.peer)?null:Get(e.peer);if(item!=null){item.Online=false;item.ConnectionState=ClassifyConnectionError(e.text);item.Refresh();}Status=e.text;}
        if(e.kind==9&&e.groups!=null){
            var visible=new HashSet<string>(e.groups.Select(group=>group.id));
            foreach(var old in GroupChats.Keys.Where(id=>!visible.Contains(id)).ToList())GroupChats.Remove(old);
            foreach(var data in e.groups){GroupModel group;if(!GroupChats.TryGetValue(data.id,out group)){group=new GroupModel{Data=data};GroupChats.Add(data.id,group);}else group.Data=data;group.Invited=data.invites!=null&&data.invites.Any(member=>member.peer==OwnId);group.Refresh();}
            if(SelectedGroup!=null&&!GroupChats.ContainsKey(SelectedGroup.Id))SelectedGroup=null;
        }
        if((e.kind==10||e.kind==11)&&e.groupEvents!=null){
            GroupModel group;
            if(GroupChats.TryGetValue(e.peer,out group)){foreach(var value in e.groupEvents){if(group.Messages.Any(existing=>existing.Data.id==value.id))continue;bool outgoing=value.sender==OwnId;var member=(group.Data.members??new GroupMemberData[0]).FirstOrDefault(item=>item.account==value.account);string text=!outgoing&&member!=null?member.name+"\n"+value.text:value.text;var data=new MessageData{id=value.id,sender=value.sender,text=text,time=value.time,logical=value.logical,delivery=2};var added=new MessageModel{Data=data,Outgoing=outgoing};group.Messages.Add(added);if(e.kind==10&&!added.Outgoing&&(SelectedGroup!=group||!Groups))group.Unread++;}OrderMessages(group.Messages);group.Refresh();}
        }
    }
    static string ClassifyConnectionError(string text){
        string value=(text??"").ToLowerInvariant();
        if(value.Contains("dns")||value.Contains("hostname")||value.Contains("resolve"))return "DNS/hostname failure";
        if(value.Contains("refused"))return "TCP refused";
        if(value.Contains("timeout")||value.Contains("timed out"))return "TCP timeout";
        if(value.Contains("unreachable")||value.Contains("network route"))return "Offline / unreachable";
        if(value.Contains("tls")||value.Contains("secure session"))return "TLS handshake failure";
        if(value.Contains("identity")||value.Contains("trust"))return "Identity mismatch";
        return "Connection failed";
    }
}
}
