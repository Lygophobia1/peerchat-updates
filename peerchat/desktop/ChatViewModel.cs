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
    public readonly HashSet<string> HistoryExhaustedPeers=new HashSet<string>(StringComparer.Ordinal);
    // History pages are requested per device peer. An outgoing bubble does not
    // carry its storage peer, so retain each page's authenticated cursor here.
    readonly Dictionary<string,MessageData> historyAnchors=new Dictionary<string,MessageData>(StringComparer.Ordinal);
    public long HistoryCursor(string peer){MessageData anchor;return historyAnchors.TryGetValue(peer,out anchor)?anchor.sequence:long.MaxValue;}
    public void ResetHistoryPaging(){historyAnchors.Clear();}
    public void ObserveHistoryPage(string peer,IEnumerable<MessageData> page){
        if(String.IsNullOrEmpty(peer)||page==null)return;
        var earliest=page.Where(message=>message!=null&&message.sequence>0)
            .OrderBy(message=>message.time).ThenBy(message=>message.sender,StringComparer.Ordinal)
            .ThenBy(message=>message.id,StringComparer.Ordinal).FirstOrDefault();
        if(earliest==null)return;
        MessageData prior;
        if(!historyAnchors.TryGetValue(peer,out prior)||earliest.time<prior.time||
           (earliest.time==prior.time&&(StringComparer.Ordinal.Compare(earliest.sender,prior.sender)<0||
           (earliest.sender==prior.sender&&StringComparer.Ordinal.Compare(earliest.id,prior.id)<0))))
            historyAnchors[peer]=earliest;
    }
    public readonly ObservableCollection<MessageModel> Messages=new ObservableCollection<MessageModel>();
    public string Draft="";
    public bool Online,RequestedHistory,HistoryLoading,HistoryExhausted;
    public bool? ServerOnline;
    public string LoadingHistoryPeer="";
    public long WinningConnectionId;
    public int ReliableRoute;
    public int P2pAvailability;
    public string ReliableEndpoint="";
    public long SessionEpoch,SessionRevision;
    // Presentation state mirrors the core's connection lifecycle. It is deliberately
    // separate from account/friend state so a direct P2P chat can work offline.
    public string ConnectionState="Offline";
    public int Unread;
    public string Name {get{return !String.IsNullOrEmpty(AccountName)?AccountName:Contact.name;}}
    public string Initial {get{return String.IsNullOrEmpty(Name)?"?":System.Globalization.StringInfo.GetNextTextElement(Name).ToUpperInvariant();}}
    public Brush Presence {get{return new SolidColorBrush((IsFriend?ServerOnline==true:Online)?Color.FromRgb(54,216,115):Color.FromRgb(107,121,135));}}
    public string Preview {get{return Messages.Count>0?Messages.Last().Preview:(Contact.verified?"Start a conversation":"Awaiting verification");}}
    public string When {get{return Messages.Count>0?Messages.Last().ShortTime:"";}}
    public string UnreadLabel {get{return Unread>0?Unread+" unread":"";}}
    public void Refresh(){foreach(string name in new[]{"Name","Initial","Presence","Preview","When","UnreadLabel","ConnectionState"})Changed(name);}
}
public class GroupModel : ObservableModel {
    public GroupData Data;
    public readonly ObservableCollection<MessageModel> Messages=new ObservableCollection<MessageModel>();
    public string Draft="";
    public int Unread;
    public bool RequestedHistory,HistoryLoading,HistoryExhausted;
    public bool Invited;
    public string Id {get{return Data.id;}}
    public string Name {get{return Data.name;}}
    public string Initial {get{return String.IsNullOrEmpty(Name)?"G":System.Globalization.StringInfo.GetNextTextElement(Name).ToUpperInvariant();}}
    public Brush Presence {get{return new SolidColorBrush(Color.FromRgb(66,151,232));}}
    public string Preview {get{return Messages.Count>0?Messages.Last().Preview:(Invited?"Invitation · Accept or decline":MemberCount+" members"+(PendingInviteCount>0?" · "+PendingInviteCount+" pending":""));}}
    public string When {get{return Messages.Count>0?Messages.Last().ShortTime:"";}}
    public string UnreadLabel {get{return Unread>0?Unread+" unread":"";}}
    public int MemberCount {get{return Data.members==null?0:Data.members.Length;}}
    public int PendingInviteCount {get{return Data.invites==null?0:Data.invites.Length;}}
    public void Refresh(){foreach(string name in new[]{"Name","Initial","Presence","Preview","When","UnreadLabel","MemberCount","PendingInviteCount"})Changed(name);}
}
public class MessageModel : ObservableModel {
    public sealed class DeliveryDeviceRow {
        public string Label {get;set;}
        public string FullIdentity {get;set;}
        public string State {get;set;}
    }
    public CoreAdapter.FileTransferData Attachment {get;private set;}
    public CoreAdapter.GroupAttachmentData GroupAttachment {get;private set;}
    public string GroupAttachmentRevision {get;private set;}
    public void SetGroupAttachment(CoreAdapter.GroupAttachmentData value,string revision){GroupAttachment=value;GroupAttachmentRevision=revision;Changed("GroupAttachment");Changed("TextVisibility");Changed("Text");Changed("Preview");Changed("Stamp");}
    public Visibility TextVisibility {get{return Attachment==null&&GroupAttachment==null?Visibility.Visible:Visibility.Collapsed;}}
    public void SetAttachment(CoreAdapter.FileTransferData value){Attachment=value;Changed("Attachment");Changed("TextVisibility");Changed("Preview");Changed("Stamp");}
    public MessageData Data;
    // Null means that this row has no authoritative recipient manifest (legacy
    // history and imported messages retain their original aggregate stamp).
    DeliveryTargetsData deliveryTargets;
    public Visibility DeliveryVisibility {get{return Outgoing&&!IsGroupMessage&&deliveryTargets!=null?Visibility.Visible:Visibility.Collapsed;}}
    public DeliveryDeviceRow[] DeliveryRows {get{
        if(deliveryTargets==null)return new DeliveryDeviceRow[0];
        return ValidTargets(deliveryTargets).Select(target=>new DeliveryDeviceRow{
            Label="Device "+target.device.Substring(0,12)+"…",
            FullIdentity="Device identity: "+target.device,
            State=target.status==1?"Received":target.status==2?"Unavailable":"Pending"
        }).ToArray();
    }}
    public string DeliverySummary {get{
        if(deliveryTargets==null)return "";
        var targets=ValidTargets(deliveryTargets).ToArray();
        int received=targets.Count(target=>target.status==1),pending=targets.Count(target=>target.status==0),unavailable=targets.Count(target=>target.status==2);
        string summary=targets.Length==0?"No recipient devices selected":"Received by "+received+" of "+targets.Length+" "+(targets.Length==1?"device":"devices");
        if(pending>0)summary+=" · "+pending+" pending";
        if(unavailable>0)summary+=" · "+unavailable+" unavailable";
        if(deliveryTargets.selectionUnresolved)summary+=" · recipient selection unresolved";
        return summary;
    }}
    static IEnumerable<DeliveryTargetData> ValidTargets(DeliveryTargetsData data){
        var seen=new HashSet<string>(StringComparer.Ordinal);
        return (data.targets??new DeliveryTargetData[0]).Where(target=>target!=null&&target.device!=null&&target.device.Length==64&&
            target.device.All(Uri.IsHexDigit)&&target.status>=0&&target.status<=2&&seen.Add(target.device));
    }
    public bool Outgoing;
    public long GroupSenderAccountId;
    public bool IsGroupMessage;
    string groupSenderName="";
    public void SetGroupSenderName(string name){if(groupSenderName==name)return;groupSenderName=name;Changed("Text");Changed("Preview");}
    double bubbleWidth=400;
    public double BubbleWidth {get{return bubbleWidth;}set{bubbleWidth=value;Changed("BubbleWidth");}}
    public string Text {get{return GroupAttachment!=null?GroupAttachment.name:Attachment!=null?(ChatAttachmentProjection.IsLongMessage(Attachment)?"Long message":Attachment.name):IsGroupMessage?groupSenderName+"\n"+Data.text:Data.text;}}
    public string Preview {get{string value=Text??"";if(value.Length<=160)return value;int length=160;if(Char.IsHighSurrogate(value[length-1])&&Char.IsLowSurrogate(value[length]))length--;return value.Substring(0,length)+"…";}}
    public HorizontalAlignment Alignment {get{return Outgoing?HorizontalAlignment.Right:HorizontalAlignment.Left;}}
    public Brush Bubble {get{return new SolidColorBrush(Outgoing?Color.FromRgb(27,73,125):Color.FromRgb(31,42,52));}}
    public DateTime LocalTime {get{try{return new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddMilliseconds(Data.time).ToLocalTime();}catch{return DateTime.MinValue;}}}
    public string ShortTime {get{return Data.status=="attachment_reference"?"Time unavailable":LocalTime.Date==DateTime.Today?LocalTime.ToString("h:mm tt"):LocalTime.ToString("MMM d");}}
    public string Stamp {get{return GroupAttachment!=null?LocalTime.ToString("MMM d · h:mm tt")+" · Group attachment":Data.status=="attachment_reference"?"Authenticated reference · original time/order unavailable":Attachment!=null?(ChatAttachmentProjection.IsLongMessage(Attachment)?LocalTime.ToString("MMM d · h:mm tt"):"File attachment"):LocalTime.ToString("MMM d · h:mm tt")+(Outgoing&&deliveryTargets==null?(!String.IsNullOrEmpty(Data.status)?"  "+Data.status:Data.delivery==2?"  ✓✓ Delivered":Data.delivery==3?"  ◷ Stored · awaiting recipient":Data.delivery==1?"  ◷ Sent · delivery not fully confirmed":"  ◷ Queued"):"");}}
    public void Delivered(){Data.delivery=2;Changed("Stamp");}
    public void ProjectNativeDelivery(int value,DeliveryTargetsData targets=null){
        Func<int,int> rank=state=>state==2?3:state==3?2:state==1?1:0;
        if(value>=0&&value<=3&&rank(value)>rank(Data.delivery)){Data.delivery=value;Changed("Stamp");}
        if(targets!=null){deliveryTargets=targets;Data.deliveryTargets=targets;Changed("DeliveryVisibility");Changed("DeliverySummary");Changed("DeliveryRows");Changed("Stamp");}
    }
    public void InitializeNativeDelivery(){if(Data!=null&&Data.deliveryTargets!=null)ProjectNativeDelivery(Data.delivery,Data.deliveryTargets);}
    public void GroupStatus(string status){Data.status=status;Changed("Stamp");}
}
// Dispatcher-confined presentation state. Core events, never the GUI, determine
// online/trust/delivery state. Switching views retains drafts and history.
public class ChatViewModel {
    public readonly Dictionary<string,ConversationModel> Conversations=new Dictionary<string,ConversationModel>();
    readonly Dictionary<string,ConversationModel> conversationsByPeer=new Dictionary<string,ConversationModel>(StringComparer.Ordinal);
    public readonly Dictionary<string,GroupModel> GroupChats=new Dictionary<string,GroupModel>();
    // Display-only projection of authenticated server rosters. Never use ledger
    // labels or message metadata as account identity, and never rewrite message IDs.
    readonly Dictionary<string,Dictionary<long,Dictionary<string,string>>> groupSenderNames=new Dictionary<string,Dictionary<long,Dictionary<string,string>>>(StringComparer.Ordinal);
    public void ClearGroupSenderNames(){groupSenderNames.Clear();RefreshGroupSenderNames();}
    public void SetGroupSenderRoster(ServerGroupRoster roster){
        if(roster==null||String.IsNullOrEmpty(roster.LegacyGroupId))return;
        var accounts=new Dictionary<long,Dictionary<string,string>>();
        foreach(var member in roster.Members.Where(value=>value.State=="member"&&value.AccountId>0&&!String.IsNullOrEmpty(value.PeerId))){
            Dictionary<string,string> devices;
            if(!accounts.TryGetValue(member.AccountId,out devices))accounts[member.AccountId]=devices=new Dictionary<string,string>(StringComparer.Ordinal);
            devices[member.PeerId]=member.Username;
        }
        groupSenderNames[roster.LegacyGroupId]=accounts;
        GroupModel group;if(GroupChats.TryGetValue(roster.LegacyGroupId,out group))RefreshGroupSenderNames(group);
    }
    string GroupSenderName(string groupId,long accountId,string peerId){
        Dictionary<long,Dictionary<string,string>> accounts;Dictionary<string,string> devices;string name;
        return groupSenderNames.TryGetValue(groupId,out accounts)&&accounts.TryGetValue(accountId,out devices)&&devices.TryGetValue(peerId??"",out name)&&!String.IsNullOrWhiteSpace(name)?name:"Account "+accountId;
    }
    void RefreshGroupSenderNames(GroupModel group){foreach(var message in group.Messages)if(message.IsGroupMessage)message.SetGroupSenderName(GroupSenderName(group.Id,message.GroupSenderAccountId,message.Data.sender));group.Refresh();}
    void RefreshGroupSenderNames(){foreach(var group in GroupChats.Values)RefreshGroupSenderNames(group);}
    public string OwnId="",Name="You",Status="Opening your encrypted profile…";
    public ushort Port=7777;
    public bool Listening,Groups;
    // Local enqueue eligibility, not route readiness. Historical/revoked account
    // conversations remain readable but cannot send; legacy verified direct
    // contacts retain their existing profile-local behavior.
    public bool CanQueueDirectText(bool accountAuthorized,bool busy){
        var selected=Selected;
        return !Groups&&Listening&&!busy&&selected!=null&&selected.Contact!=null&&
            !String.IsNullOrEmpty(selected.Contact.peer)&&(selected.AccountId==0?selected.Contact.verified:(accountAuthorized&&selected.IsFriend));
    }
    public string VoiceState="Idle",VoiceCallId="",VoicePeerId="";
    public int VoiceStateCode,VoiceCallType;
    public long VoiceCallLifetime;
    public int VoiceMediaState,VoiceMediaRoute;
    public long VoiceMediaGeneration;
    public bool VoiceControlAvailable=true;
    public bool VoiceMuted,VoiceDeafened;
    public ConversationModel Selected;
    public GroupModel SelectedGroup;
    public ConversationModel Get(string peer){ConversationModel result;if(!String.IsNullOrEmpty(peer)&&conversationsByPeer.TryGetValue(peer,out result))return result;string key="peer:"+(peer??"");result=new ConversationModel{ConversationId=key,Contact=new ContactData{peer=peer,name="Friend",host=""}};if(!String.IsNullOrEmpty(peer)){result.KnownPeerIds.Add(peer);conversationsByPeer[peer]=result;}Conversations[key]=result;return result;}
    public ConversationModel FindByPeer(string peer){ConversationModel result;return !String.IsNullOrEmpty(peer)&&conversationsByPeer.TryGetValue(peer,out result)?result:null;}
    public ConversationModel BindFriend(long accountId,string username,string currentPeer,IEnumerable<string> knownPeers,out bool reused){
        if(accountId<=0)throw new ArgumentOutOfRangeException("accountId");string key="account:"+accountId;ConversationModel conversation;reused=Conversations.TryGetValue(key,out conversation);
        ConversationModel peerConversation=null;if(!String.IsNullOrEmpty(currentPeer))conversationsByPeer.TryGetValue(currentPeer,out peerConversation);
        if(!reused&&peerConversation!=null){conversation=peerConversation;Conversations.Remove(conversation.ConversationId);reused=true;}
        if(!reused){conversation=new ConversationModel{Contact=new ContactData{peer=currentPeer,name=username,host=""}};}
        if(peerConversation!=null&&peerConversation!=conversation){foreach(var message in peerConversation.Messages)if(!conversation.Messages.Any(existing=>(existing.Attachment==null)==(message.Attachment==null)&&existing.Data.id==message.Data.id))conversation.Messages.Add(message);foreach(var peer in peerConversation.KnownPeerIds){conversation.KnownPeerIds.Add(peer);conversationsByPeer[peer]=conversation;}if(String.IsNullOrEmpty(conversation.Draft))conversation.Draft=peerConversation.Draft;conversation.Unread+=peerConversation.Unread;Conversations.Remove(peerConversation.ConversationId);}
        conversation.ConversationId=key;conversation.AccountId=accountId;conversation.AccountName=username??"";conversation.IsFriend=true;Conversations[key]=conversation;
        if(knownPeers!=null)foreach(var peer in knownPeers)if(!String.IsNullOrEmpty(peer)){conversation.KnownPeerIds.Add(peer);conversationsByPeer[peer]=conversation;}
        if(conversation.Contact==null)conversation.Contact=new ContactData{peer=currentPeer,name=username,host=""};
        string previous=conversation.Contact.peer??"";bool changed=!String.IsNullOrEmpty(previous)&&!String.IsNullOrEmpty(currentPeer)&&previous!=currentPeer;
        if(changed&&conversation.Contact.verified)conversation.PreviousVerifiedPeerId=previous;
        if(!String.IsNullOrEmpty(currentPeer)){conversation.KnownPeerIds.Add(currentPeer);conversationsByPeer[currentPeer]=conversation;if(changed)conversation.Contact=new ContactData{peer=currentPeer,name=username,host="",verified=false};else{conversation.Contact.peer=currentPeer;conversation.Contact.name=username;}}
        OrderMessages(conversation.Messages);conversation.Refresh();return conversation;
    }
    public void ReconcileFriends(ISet<long> activeAccounts){foreach(var conversation in Conversations.Values)if(conversation.AccountId>0)conversation.IsFriend=activeAccounts.Contains(conversation.AccountId);}
    // Social eligibility is independent of presence, route availability and trust.
    // Trust is enforced when signing the invitation, not by hiding the friend.
    public IEnumerable<ConversationModel> GroupInviteCandidates(GroupData group){
        var excluded=new HashSet<long>((group.members??new GroupMemberData[0]).Concat(group.invites??new GroupMemberData[0]).Select(member=>member.account));
        return Conversations.Values.Where(friend=>friend.IsFriend&&friend.AccountId>0&&!excluded.Contains(friend.AccountId))
            .OrderBy(friend=>friend.Name,StringComparer.OrdinalIgnoreCase).ToArray();
    }
    public void RemoveConversation(ConversationModel conversation){if(conversation==null)return;Conversations.Remove(conversation.ConversationId);foreach(var peer in conversation.KnownPeerIds.ToArray())if(conversationsByPeer.ContainsKey(peer)&&conversationsByPeer[peer]==conversation)conversationsByPeer.Remove(peer);}
    static void OrderMessages(ObservableCollection<MessageModel> messages){
        var ordered=messages.Where(m=>m.Attachment==null).OrderBy(m=>m.Data.time).ThenBy(m=>m.Data.sender,StringComparer.Ordinal).ThenBy(m=>m.Data.id,StringComparer.Ordinal).ToList();
        foreach(var file in messages.Where(m=>m.Attachment!=null).OrderBy(m=>m.Data.time).ThenBy(m=>m.Data.id,StringComparer.Ordinal)){
            int position=ordered.FindIndex(item=>item.Data.time>file.Data.time);if(position<0)ordered.Add(file);else ordered.Insert(position,file);
        }
        for(int index=0;index<ordered.Count;index++){int current=messages.IndexOf(ordered[index]);if(current!=index)messages.Move(current,index);}
    }
    public void Apply(CoreEvent e){
        if((e.kind==5||e.kind==6)&&e.messages!=null)foreach(var conversation in Conversations.Values)foreach(var value in e.messages){var existing=conversation.Messages.FirstOrDefault(m=>m.Attachment==null&&m.Data.id==value.id&&m.Data.sender==value.sender);if(existing!=null)existing.ProjectNativeDelivery(value.delivery,value.deliveryTargets);}
        // Media-device failures do not change the authenticated text session.
        if(e.kind==8&&(e.text??"").StartsWith("Voice audio device error:",StringComparison.Ordinal)){Status=e.text;return;}
        if(e.kind==0){OwnId=e.peer;Name=e.text;Port=e.port;Listening=true;Status="Ready to connect";}
        if(e.kind==1&&e.contacts!=null)foreach(var contact in e.contacts){var item=Get(contact.peer);item.KnownPeerIds.Add(contact.peer);if(item.AccountId==0||item.Contact==null||item.Contact.peer==contact.peer){string accountName=item.AccountName;item.Contact=contact;if(!String.IsNullOrEmpty(accountName))item.Contact.name=accountName;if(contact.verified)item.PreviousVerifiedPeerId="";}item.Refresh();}
        if(e.kind==3){var item=Get(e.peer);if(item.SessionRevision==0){item.Online=true;item.WinningConnectionId=e.attempt;item.ConnectionState="Ready";item.Refresh();}Status=e.text+" connected";}
        if(e.kind==4){var item=!String.IsNullOrEmpty(e.expected)?FindByPeer(e.expected):null;if(item==null)item=Get(e.peer);if(item.SessionRevision==0&&(!item.Online||e.attempt==0||e.attempt==item.WinningConnectionId)){item.Online=false;item.WinningConnectionId=0;item.ConnectionState=(e.text??"").IndexOf("identity mismatch",StringComparison.OrdinalIgnoreCase)>=0?"Identity verification required":"Retrying with backoff";item.Refresh();Status=e.text??"Peer disconnected. Reconnect is available.";}}
        if(e.kind==2){var item=!String.IsNullOrEmpty(e.expected)?FindByPeer(e.expected):null;if(item==null&&String.IsNullOrEmpty(e.expected))item=Get(e.peer);if(item!=null){if((String.IsNullOrEmpty(e.expected)||e.expected==e.peer)&&e.contacts!=null&&e.contacts.Length>0)item.Contact=e.contacts[0];if(!item.Online){item.ConnectionState="Awaiting peer verification";item.Refresh();}}}
        if(e.kind==9)Status=e.text;
        if((e.kind==5||e.kind==6)&&e.messages!=null){var item=Get(e.peer);if(e.kind==6&&e.text=="history_cleared"){item.Messages.Clear();item.ResetHistoryPaging();item.RequestedHistory=false;item.HistoryLoading=false;item.HistoryExhausted=false;item.LoadingHistoryPeer="";item.RequestedPeerHistory.Clear();item.HistoryExhaustedPeers.Clear();item.Refresh();return;}if(e.kind==6)item.ObserveHistoryPage(e.peer,e.messages);foreach(var message in e.messages){if(item.Messages.Any(m=>m.Attachment==null&&m.Data.id==message.id))continue;var model=new MessageModel{Data=message,Outgoing=message.sender==OwnId||message.localAuthor};model.InitializeNativeDelivery();item.Messages.Add(model);if(e.kind==5&&!model.Outgoing&&(Selected!=item||Groups))item.Unread++;}if(e.kind==6){item.HistoryLoading=false;item.LoadingHistoryPeer="";if(e.messages.Length<100)item.HistoryExhaustedPeers.Add(e.peer);var peers=item.KnownPeerIds.Concat(new[]{item.Contact==null?"":item.Contact.peer}).Where(peer=>!String.IsNullOrEmpty(peer)).Distinct(StringComparer.Ordinal);item.HistoryExhausted=peers.All(peer=>item.HistoryExhaustedPeers.Contains(peer));}OrderMessages(item.Messages);item.Refresh();}
        // NodeEvent::Delivered is 7 and Error is 8 (the enum is the ABI contract).
        if(e.kind==7&&e.messages!=null)foreach(var item in Conversations.Values)foreach(var receipt in e.messages){var match=item.Messages.FirstOrDefault(m=>m.Attachment==null&&m.Data.id==receipt.id&&m.Data.sender==receipt.sender);if(match!=null)match.ProjectNativeDelivery(receipt.delivery,receipt.deliveryTargets);}
        if(e.kind==8){var item=!String.IsNullOrEmpty(e.expected)?FindByPeer(e.expected):null;if(item==null&&!String.IsNullOrEmpty(e.peer)&&String.IsNullOrEmpty(e.expected))item=FindByPeer(e.peer);if(item!=null&&!item.Online&&item.SessionRevision==0){item.ConnectionState=ClassifyConnectionError(e.text);item.Refresh();}Status=e.text;}
        if(e.kind==9&&e.groups!=null){
            var visible=new HashSet<string>(e.groups.Select(group=>group.id));
            foreach(var old in GroupChats.Keys.Where(id=>!visible.Contains(id)).ToList())GroupChats.Remove(old);
            foreach(var data in e.groups){GroupModel group;if(!GroupChats.TryGetValue(data.id,out group)){group=new GroupModel{Data=data};GroupChats.Add(data.id,group);}else group.Data=data;group.Invited=data.invites!=null&&data.invites.Any(member=>member.peer==OwnId);group.Refresh();}
            if(SelectedGroup!=null&&!GroupChats.ContainsKey(SelectedGroup.Id))SelectedGroup=null;
        }
        if((e.kind==10||e.kind==11)&&e.groupEvents!=null){
            GroupModel group;
            if(GroupChats.TryGetValue(e.peer,out group)){foreach(var value in e.groupEvents){if(group.Messages.Any(existing=>existing.Data.id==value.id))continue;bool outgoing=value.sender==OwnId;var data=new MessageData{id=value.id,sender=value.sender,text=value.text,time=value.time,logical=value.logical,sequence=value.sequence>0?value.sequence:value.logical,status=value.status,delivery=outgoing?0:2};var added=new MessageModel{Data=data,Outgoing=outgoing,IsGroupMessage=true,GroupSenderAccountId=value.account};added.SetGroupSenderName(GroupSenderName(group.Id,value.account,value.sender));group.Messages.Add(added);if(e.kind==10&&!added.Outgoing&&(SelectedGroup!=group||!Groups))group.Unread++;}if(e.kind==11){bool indexing=e.text=="Preparing protected group history ordering";group.HistoryLoading=indexing;if(!indexing)group.HistoryExhausted=e.text!="More group history available"&&e.groupEvents.Length<100;}OrderMessages(group.Messages);group.Refresh();}
        }
        if(e.kind==12){GroupModel group;if(GroupChats.TryGetValue(e.peer,out group)){var message=group.Messages.FirstOrDefault(m=>m.Data.id==e.detail);if(message!=null)message.GroupStatus(e.text);group.Refresh();}}
        if(e.kind==13){if(e.callLifetime<VoiceCallLifetime||(e.callLifetime==VoiceCallLifetime&&e.mediaGeneration<VoiceMediaGeneration))return;VoiceCallLifetime=e.callLifetime;VoiceStateCode=e.callState;VoiceCallType=e.callType;VoiceMediaState=e.mediaState;VoiceMediaGeneration=e.mediaGeneration;VoiceMediaRoute=e.mediaRoute;VoiceControlAvailable=e.callControlAvailable;VoiceState=e.text??"Idle";VoiceCallId=e.detail??"";VoicePeerId=e.peer??"";VoiceMuted=e.callMuted;VoiceDeafened=e.callDeafened;}
        if(e.kind==17){var item=!String.IsNullOrEmpty(e.expected)?FindByPeer(e.expected):null;if(item==null)item=Get(e.peer);bool newer=e.sessionEpoch>item.SessionEpoch||(e.sessionEpoch==item.SessionEpoch&&e.sessionRevision>item.SessionRevision);if(newer){item.SessionEpoch=e.sessionEpoch;item.SessionRevision=e.sessionRevision;item.Online=e.connected;item.WinningConnectionId=e.connected?e.attempt:0;item.ReliableRoute=e.connected?e.reliableRoute:0;item.P2pAvailability=e.p2pAvailability;item.ReliableEndpoint=e.connected?(e.candidate??""):"";item.ConnectionState=String.IsNullOrEmpty(e.text)?(e.connected?"Ready":"Offline"):e.text;item.Refresh();}}
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
