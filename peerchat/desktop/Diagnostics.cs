using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
public sealed class DiagnosticsSection {
    public string Name {get;set;}
    public string Details {get;set;}
}
public sealed class DiagnosticsSnapshot {
    public string SnapshotId {get;set;}
    public string TimestampUtc {get;set;}
    public string Build {get;set;}
    public string Profile {get;set;}
    public string Status {get;set;}
    public List<DiagnosticsSection> Sections {get;set;}
    public List<GroupDiagnosticsData> Groups {get;set;}
}
// A frozen projection of the signed group ledger and any separately authorized
// server roster state. No key bytes, chat content, or route endpoints belong here.
public sealed class GroupDiagnosticsData {
    public string GroupId {get;set;}
    public string ServerGroupId {get;set;}
    public string Name {get;set;}
    public string LocalRole {get;set;}
    public string RosterState {get;set;}
    public string GroupControlState {get;set;}
    public string E2eeState {get;set;}
    public string DeliveryState {get;set;}
    public string KeyHandoffState {get;set;}
    public string LastKeyOfferResult {get;set;}
    public string LastKeyAckResult {get;set;}
    public long RosterGeneration {get;set;}
    public long KeyGeneration {get;set;}
    public int PendingInvites {get;set;}
    public int KnownOnlineMembers {get;set;}
    public int MembersWithKnownPresence {get;set;}
    public List<GroupDiagnosticsMember> Members {get;set;}
}
public sealed class GroupDiagnosticsMember {
    public long AccountId {get;set;}
    public string Name {get;set;}
    public string PeerId {get;set;}
    public string Role {get;set;}
    public string Relationship {get;set;}
    public string GroupControlState {get;set;}
    public string KeyHandoffState {get;set;}
}
// Stable logical ownership for a diagnostics projection.  This deliberately has
// no endpoint, ICE candidate, allocation, or transport-generation fields: those
// routes may change while the PeerChat conversation remains the same.
public sealed class DiagnosticsConnectionScope {
    public string Profile {get;set;}
    public string AccountId {get;set;}
    public string ConversationId {get;set;}
    public string RemotePeerId {get;set;}
    // Presentation-only metadata. Never participates in Key or scope matching.
    public string AccountDisplayName {get;set;}
    public string Key {get{return (Profile??"")+"|"+(AccountId??"")+"|"+(ConversationId??"")+"|"+(RemotePeerId??"");}}
    public string Label {
        get {
            string peer=String.IsNullOrEmpty(RemotePeerId)?"peer not yet presented":RemotePeerId;
            string conversation=DisplayConversation(ConversationId,AccountDisplayName);
            if(String.IsNullOrEmpty(conversation))conversation="conversation unavailable";
            return conversation+" · "+peer;
        }
    }
    static string DisplayConversation(string conversation,string username){if(String.IsNullOrEmpty(conversation))return "";long id;const string prefix="account:";if(conversation.StartsWith(prefix,StringComparison.OrdinalIgnoreCase)&&Int64.TryParse(conversation.Substring(prefix.Length),out id)&&id>0&&!String.IsNullOrWhiteSpace(username))return username+":"+id;return conversation;}
}
public sealed class FailureSnapshot {
    public string Id {get;set;}
    public string TimestampUtc {get;set;}
    public string Title {get;set;}
    public string OperationType {get;set;}
    public string OperationId {get;set;}
    public string FailureDomain {get;set;}
    public string Reason {get;set;}
    public string Stage {get;set;}
    public long Generation {get;set;}
    public string Module {get;set;}
    public DiagnosticsSnapshot Diagnostics {get;set;}
}
public sealed class ActiveDiagnosticOperation {
    public string OperationType {get;set;}
    public string OperationId {get;set;}
    public string Stage {get;set;}
    public long Generation {get;set;}
    public string Module {get;set;}
    public string UpdatedUtc {get;set;}
    public DiagnosticsSnapshot Diagnostics {get;set;}
}

public sealed class DiagnosticsSources {
    public Func<string> Connection {get;set;}
    public Func<string> ConnectionEvents {get;set;}
    public Func<string> Security {get;set;}
    public Func<string> Voice {get;set;}
    public Func<VoiceDiagnosticsData> VoiceData {get;set;}
    public Func<string> ScreenShare {get;set;}
    public Func<string> Local {get;set;}
    public Func<string> Status {get;set;}
    public Func<string> Build {get;set;}
    public Func<string> Profile {get;set;}
    public Func<IList<GroupDiagnosticsData>> Groups {get;set;}
}
public sealed class VoiceDiagnosticsData {
    public string Native,CallId,PeerId,VerifiedPeerId,CallState,MediaRoute,InputDevice,OutputDevice,StartedUtc,ConnectedUtc,EndedUtc,LatestAudioError;
    public long Lifetime,MediaGeneration,MediaState;
    public bool Active,ControlAvailable,Muted,Deafened;
}

// Collects subsystem-owned snapshots, freezes independent copies, and formats the
// shared View/Copy/Save representation. It does not own connection or media state.
public sealed class DiagnosticsService {
    readonly DiagnosticsSources sources;
    public DiagnosticsService(DiagnosticsSources value){if(value==null)throw new ArgumentNullException("value");sources=value;}
    public DiagnosticsSnapshot Capture(){
        var sections=new List<DiagnosticsSection>();
        Add(sections,"Connection",Read(sources.Connection));Add(sections,"Connection events",Read(sources.ConnectionEvents));
        Add(sections,"Security",Read(sources.Security));
        var voice=ReadVoice();Add(sections,"Voice",String.IsNullOrWhiteSpace(voice)?Read(sources.Voice):voice);
        Add(sections,"Screen share",Read(sources.ScreenShare));Add(sections,"Local",Read(sources.Local));
        return new DiagnosticsSnapshot{SnapshotId=Guid.NewGuid().ToString("N"),TimestampUtc=DateTime.UtcNow.ToString("O"),Build=Read(sources.Build),Profile=Read(sources.Profile),Status=Read(sources.Status),Sections=sections,Groups=ReadGroups()};
    }
    List<GroupDiagnosticsData> ReadGroups(){try{return CloneGroups(sources.Groups==null?null:sources.Groups());}catch{return new List<GroupDiagnosticsData>();}}
    string ReadVoice(){try{if(sources.VoiceData==null)return "";return FormatVoice(sources.VoiceData());}catch(Exception error){return "Unavailable: "+error.GetType().Name;}}
    static string FormatVoice(VoiceDiagnosticsData v){
        if(v==null)return "";return "call_id="+(v.CallId??"none")+"\ncall_lifetime="+v.Lifetime+"\ncall_type=voice\nexpected_peer_id="+(v.PeerId??"none")+"\nverified_peer_id="+(v.VerifiedPeerId??"not verified/available")+"\ncall_state="+(v.CallState??"Idle")+"\nmedia_generation="+v.MediaGeneration+"\nmedia_state="+v.MediaState+"\nmedia_route="+(v.MediaRoute??"Unavailable")+"\ncontrol_available="+v.ControlAvailable+"\ninput_device="+(v.InputDevice??"none")+"\noutput_device="+(v.OutputDevice??"none")+"\nmuted="+v.Muted+"\ndeafened="+v.Deafened+"\nstarted_utc="+(v.StartedUtc??"unknown")+"\nconnected_utc="+(v.ConnectedUtc??"unknown")+"\nended_utc="+(v.EndedUtc??"unknown")+"\n"+(v.Native??"media_state=unavailable")+"\nlatest_persistent_audio_error="+(v.LatestAudioError??"None");
    }
    public FailureSnapshot FreezeFailure(string title,string operationType,string operationId,string domain,string reason,string stage,long generation,DiagnosticsSnapshot snapshot){
        return new FailureSnapshot{Id=Guid.NewGuid().ToString("N"),TimestampUtc=DateTime.UtcNow.ToString("O"),Title=DiagnosticsPrivacyFilter.Redact(Safe(title,160)),OperationType=Safe(operationType,64),OperationId=DiagnosticsPrivacyFilter.Redact(Safe(operationId,160)),FailureDomain=Safe(domain,96),Reason=DiagnosticsPrivacyFilter.Redact(Safe(reason,512)),Stage=DiagnosticsPrivacyFilter.Redact(Safe(stage,256)),Generation=generation,Diagnostics=Clone(snapshot)};
    }
    public static DiagnosticsSnapshot Clone(DiagnosticsSnapshot value){
        if(value==null)return null;return new DiagnosticsSnapshot{SnapshotId=value.SnapshotId,TimestampUtc=value.TimestampUtc,Build=value.Build,Profile=value.Profile,Status=value.Status,Sections=(value.Sections??new List<DiagnosticsSection>()).Select(s=>new DiagnosticsSection{Name=s.Name,Details=s.Details}).ToList(),Groups=CloneGroups(value.Groups)};
    }
    static List<GroupDiagnosticsData> CloneGroups(IEnumerable<GroupDiagnosticsData> groups){return (groups??Enumerable.Empty<GroupDiagnosticsData>()).Where(g=>g!=null).Take(128).Select(g=>new GroupDiagnosticsData{GroupId=g.GroupId,ServerGroupId=g.ServerGroupId,Name=g.Name,LocalRole=g.LocalRole,RosterState=g.RosterState,GroupControlState=g.GroupControlState,E2eeState=g.E2eeState,DeliveryState=g.DeliveryState,KeyHandoffState=g.KeyHandoffState,LastKeyOfferResult=g.LastKeyOfferResult,LastKeyAckResult=g.LastKeyAckResult,RosterGeneration=g.RosterGeneration,KeyGeneration=g.KeyGeneration,PendingInvites=g.PendingInvites,KnownOnlineMembers=g.KnownOnlineMembers,MembersWithKnownPresence=g.MembersWithKnownPresence,Members=(g.Members??new List<GroupDiagnosticsMember>()).Where(m=>m!=null).Take(256).Select(m=>new GroupDiagnosticsMember{AccountId=m.AccountId,Name=m.Name,PeerId=m.PeerId,Role=m.Role,Relationship=m.Relationship,GroupControlState=m.GroupControlState,KeyHandoffState=m.KeyHandoffState}).ToList()}).ToList();}
    public static string FormatSummary(DiagnosticsSnapshot value){return FormatSummary(value,null,null);}
    public static string FormatSummary(DiagnosticsSnapshot value,Func<string,string> accountDisplayName){return FormatSummary(value,accountDisplayName,null);}
    public static string FormatSummary(DiagnosticsSnapshot value,Func<string,string> accountDisplayName,Func<string,string> peerDisplayName){
        if(value==null)return "PeerChat diagnostics unavailable.";
        var text=new System.Text.StringBuilder("PeerChat diagnostics\nBuild: "+Safe(value.Build,160)+"\nProfile: "+Safe(value.Profile,160)+"\nStatus: "+Safe(value.Status,320)+"\nCaptured UTC: "+Safe(value.TimestampUtc,64));var connection=(value.Sections??new List<DiagnosticsSection>()).FirstOrDefault(s=>s.Name=="Connection");if(connection!=null){string details=PresentIdentity(connection.Details,accountDisplayName);foreach(string key in new[]{"Account:","Conversation:","Account ID:","Conversation ID:","Friend presence:","Winning route:","TCP/TLS state:","Latest progress/status:","Latest failed reason:"}){string line=(details??"").Replace("\r","").Split('\n').FirstOrDefault(item=>item.StartsWith(key,StringComparison.OrdinalIgnoreCase));if(line!=null)text.AppendLine().Append(line.Trim());}}
        foreach(string sectionName in new[]{"Voice","Screen share"}){var section=(value.Sections??new List<DiagnosticsSection>()).FirstOrDefault(item=>item.Name==sectionName);string label=section==null?"":ResolvePeerDisplayLabel(section.Details,peerDisplayName);if(!String.IsNullOrEmpty(label))text.AppendLine().Append(sectionName).Append(" peer: ").Append(label);}
        if((value.Sections??new List<DiagnosticsSection>()).Any(s=>s.Name.StartsWith("Group ",StringComparison.Ordinal))&&(value.Groups??new List<GroupDiagnosticsData>()).Count==1){var group=value.Groups[0];text.AppendLine().Append("Type: Group").AppendLine().Append("Group name: ").Append(Safe(group.Name,160)).AppendLine().Append("Group ID: ").Append(Safe(group.GroupId,64));}
        var groupOverview=(value.Sections??new List<DiagnosticsSection>()).FirstOrDefault(s=>s.Name=="Group overview");
        if(groupOverview!=null)foreach(string line in Lines(groupOverview.Details).Take(12))text.AppendLine().Append(Safe(line,320));
        return DiagnosticsPrivacyFilter.Redact(text.ToString());
    }
    public static string FormatFailureSummary(FailureSnapshot value){return FormatFailureSummary(value,null,null);}
    public static string FormatFailureSummary(FailureSnapshot value,Func<string,string> accountDisplayName){return FormatFailureSummary(value,accountDisplayName,null);}
    public static string FormatFailureSummary(FailureSnapshot value,Func<string,string> accountDisplayName,Func<string,string> peerDisplayName){if(value==null)return "PeerChat diagnostics unavailable.";return DiagnosticsPrivacyFilter.Redact(Safe(value.Title,160)+"\nOperation: "+Safe(value.OperationType,64)+"\nReason: "+Safe(value.Reason,512)+"\nStage: "+Safe(value.Stage,256)+"\n"+FormatSummary(value.Diagnostics,accountDisplayName,peerDisplayName));}
    public static string FormatFailureFull(FailureSnapshot value){return FormatFailureFull(value,null,null);}
    public static string FormatFailureFull(FailureSnapshot value,Func<string,string> accountDisplayName){return FormatFailureFull(value,accountDisplayName,null);}
    public static string FormatFailureFull(FailureSnapshot value,Func<string,string> accountDisplayName,Func<string,string> peerDisplayName){if(value==null)return "PeerChat diagnostics unavailable.";return DiagnosticsPrivacyFilter.Redact("failure_id="+Safe(value.Id,160)+"\nfailure_utc="+Safe(value.TimestampUtc,64)+"\noperation_type="+Safe(value.OperationType,64)+"\noperation_id="+Safe(value.OperationId,160)+"\nfailure_domain="+Safe(value.FailureDomain,96)+"\nreason="+Safe(value.Reason,512)+"\nstage="+Safe(value.Stage,256)+"\ngeneration="+value.Generation+"\nmodule="+Safe(value.Module,160)+"\n\n"+FormatFull(value.Diagnostics,accountDisplayName,peerDisplayName));}
    public static string FormatFull(DiagnosticsSnapshot value){return FormatFull(value,null,null);}
    public static string FormatFull(DiagnosticsSnapshot value,Func<string,string> accountDisplayName){return FormatFull(value,accountDisplayName,null);}
    public static string FormatFull(DiagnosticsSnapshot value,Func<string,string> accountDisplayName,Func<string,string> peerDisplayName){
        if(value==null)return "PeerChat diagnostics unavailable.";var text=new System.Text.StringBuilder(4096);
        text.AppendLine("PeerChat full diagnostics");text.AppendLine("snapshot_utc="+Safe(value.TimestampUtc,64));text.AppendLine("build="+Safe(value.Build,160));text.AppendLine("profile="+Safe(value.Profile,160));text.AppendLine("status="+Safe(value.Status,320));
        foreach(var section in value.Sections??new List<DiagnosticsSection>()){text.AppendLine();text.AppendLine("=== "+Safe(section.Name,80)+" ===");string label=(section.Name=="Voice"||section.Name=="Screen share")?ResolvePeerDisplayLabel(section.Details,peerDisplayName):"";if(!String.IsNullOrEmpty(label))text.AppendLine("logical_peer="+label);text.AppendLine(PresentIdentity(section.Details,accountDisplayName)??"Unavailable");if(text.Length>65536){text.Length=65520;text.AppendLine("[diagnostics truncated]");break;}}
        if(!(value.Sections??new List<DiagnosticsSection>()).Any(s=>s.Name.StartsWith("Group ",StringComparison.Ordinal)))foreach(var group in GroupScopes(value)){text.AppendLine();text.AppendLine("=== Group: "+Safe(group.GroupId,64)+" ===");text.AppendLine(GroupOverview(group));if(text.Length>65536){text.Length=65520;text.AppendLine("[diagnostics truncated]");break;}}
        return DiagnosticsPrivacyFilter.Redact(text.ToString());
    }
    // The sources remain authoritative.  These helpers only project one frozen
    // snapshot into the logical session selected by the diagnostics UI.
    public static IList<DiagnosticsConnectionScope> ConnectionScopes(DiagnosticsSnapshot value){return ConnectionScopes(value,null);}
    public static IList<DiagnosticsConnectionScope> ConnectionScopes(DiagnosticsSnapshot value,Func<string,string> accountDisplayName){
        var scopes=new Dictionary<string,DiagnosticsConnectionScope>(StringComparer.Ordinal);
        if(value==null)return new List<DiagnosticsConnectionScope>();
        AddScope(scopes,ScopeFromConnectionDetails(value.Profile,Section(value,"Connection")));
        foreach(string line in Lines(Section(value,"Connection events")))AddScope(scopes,ScopeFromEvent(value.Profile,line));
        AddScope(scopes,ScopeFromMedia(value.Profile,Section(value,"Voice")));
        AddScope(scopes,ScopeFromMedia(value.Profile,Section(value,"Screen share")));
        foreach(var scope in scopes.Values){string username=ResolveAccountDisplayName(scope.AccountId,accountDisplayName);scope.AccountDisplayName=String.IsNullOrWhiteSpace(username)?"":username.Trim();}
        return scopes.Values.OrderBy(scope=>scope.ConversationId??"").ThenBy(scope=>scope.RemotePeerId??"").ToList();
    }
    public static DiagnosticsSnapshot ProjectConnection(DiagnosticsSnapshot value,DiagnosticsConnectionScope scope,string component){
        if(value==null||scope==null)return Clone(value);
        var selected=new List<DiagnosticsSection>();
        bool overview=String.Equals(component,"Overview",StringComparison.OrdinalIgnoreCase);
        if(overview||String.Equals(component,"Reliable Connection",StringComparison.OrdinalIgnoreCase)){
            string connection=Section(value,"Connection");
            if(MatchesScope(scope,ScopeFromConnectionDetails(value.Profile,connection)))Add(selected,"Connection",connection);
            string events=String.Join("\n",Lines(Section(value,"Connection events")).Where(line=>MatchesScope(scope,ScopeFromEvent(value.Profile,line))).ToArray());
            Add(selected,"Connection events",String.IsNullOrWhiteSpace(events)?"No retained events for this logical connection.":events);
        }
        if(overview||String.Equals(component,"Voice",StringComparison.OrdinalIgnoreCase)){
            string voice=Section(value,"Voice");
            if(MatchesScope(scope,ScopeFromMedia(value.Profile,voice)))Add(selected,"Voice",voice);else if(!overview)Add(selected,"Voice","No voice state for this logical connection.");
        }
        if(overview||String.Equals(component,"Screen Share",StringComparison.OrdinalIgnoreCase)){
            string screen=Section(value,"Screen share");
            if(MatchesScope(scope,ScopeFromMedia(value.Profile,screen)))Add(selected,"Screen share",screen);else if(!overview)Add(selected,"Screen share","No screen-share state for this logical connection.");
        }
        if(selected.Count==0)Add(selected,"Overview","No current subsystem state is available for this logical connection.");
        return new DiagnosticsSnapshot{SnapshotId=value.SnapshotId,TimestampUtc=value.TimestampUtc,Build=value.Build,Profile=value.Profile,Status=value.Status,Sections=selected};
    }
    public static IList<GroupDiagnosticsData> GroupScopes(DiagnosticsSnapshot value){return CloneGroups(value==null?null:value.Groups).OrderBy(g=>g.Name??"",StringComparer.OrdinalIgnoreCase).ThenBy(g=>g.GroupId??"",StringComparer.Ordinal).ToList();}
    public static DiagnosticsSnapshot ProjectGroup(DiagnosticsSnapshot value,string groupId,string component){
        if(value==null)return null;
        var group=(value.Groups??new List<GroupDiagnosticsData>()).FirstOrDefault(g=>g!=null&&String.Equals(g.GroupId,groupId,StringComparison.Ordinal));
        if(group==null)return new DiagnosticsSnapshot{SnapshotId=value.SnapshotId,TimestampUtc=value.TimestampUtc,Build=value.Build,Profile=value.Profile,Status=value.Status,Sections=new List<DiagnosticsSection>{new DiagnosticsSection{Name="Group",Details="Group diagnostics are unavailable for this snapshot."}}};
        var sections=new List<DiagnosticsSection>();string part=component??"Overview";
        if(part=="Overview")Add(sections,"Group overview",GroupOverview(group));
        else if(part=="Connection")Add(sections,"Group connection",GroupConnection(value,group));
        else if(part=="Voice")Add(sections,"Group voice","Voice room: Not implemented / Inactive\nLocal participation: Unavailable\nParticipant count: Unavailable\nJoining a future room will be voluntary; a one-person room is valid.\nNo one-to-one VoiceMedia state is attributed to this group.");
        else if(part=="Screen Share")Add(sections,"Group screen share","Screen share: Not implemented / Inactive\nActive sharer: Unavailable\nViewer count: Unavailable\nLocal viewing state: Unavailable\nViewing a future group share will be voluntary and independent of voice.\nNo one-to-one ScreenShareManager state is attributed to this group.");
        return new DiagnosticsSnapshot{SnapshotId=value.SnapshotId,TimestampUtc=value.TimestampUtc,Build=value.Build,Profile=value.Profile,Status=value.Status,Sections=sections,Groups=new List<GroupDiagnosticsData>{CloneGroups(new[]{group})[0]}};
    }
    static string GroupOverview(GroupDiagnosticsData group){
        int count=(group.Members??new List<GroupDiagnosticsMember>()).Count;
        string online=group.MembersWithKnownPresence==count?group.KnownOnlineMembers.ToString():"Unavailable (presence not known for every member)";
        return "Type: Group\nGroup name: "+Safe(group.Name,160)+"\nGroup ID: "+Safe(group.GroupId,64)+"\nMember count: "+count+"\nOnline members: "+online+"\nLocal role: "+Safe(group.LocalRole,32)+"\nRoster generation: "+Generation(group.RosterGeneration)+"\nCurrent key generation: "+Generation(group.KeyGeneration)+"\nE2EE state: "+Safe(group.E2eeState,160)+"\nPending invites: "+group.PendingInvites+"\nGroupControl: "+Safe(group.GroupControlState,120)+"\nKey handoff: "+Safe(group.KeyHandoffState,120)+"\nDelivery/health: "+Safe(group.DeliveryState,160);
    }
    static string GroupConnection(DiagnosticsSnapshot snapshot,GroupDiagnosticsData group){
        var text=new System.Text.StringBuilder();text.Append("Type: Group\nGroup name: ").Append(Safe(group.Name,160)).Append("\nGroup ID: ").Append(Safe(group.GroupId,64)).Append("\nServer roster ID: ").Append(String.IsNullOrEmpty(group.ServerGroupId)?"Unavailable":Safe(group.ServerGroupId,64)).Append("\nServer group roster: ").Append(Safe(group.RosterState,120)).Append("\nGroupControl: ").Append(Safe(group.GroupControlState,120)).Append("\nRoster generation: ").Append(Generation(group.RosterGeneration)).Append("\nCurrent key generation: ").Append(Generation(group.KeyGeneration)).Append("\nKey handoff: ").Append(Safe(group.KeyHandoffState,120)).Append("\nLast key-offer result: ").Append(Safe(group.LastKeyOfferResult,160)).Append("\nLast key-ACK result: ").Append(Safe(group.LastKeyAckResult,160)).Append("\nFanout: Not implemented (Stage 1C)");
        var scopes=ConnectionScopes(snapshot);
        foreach(var member in group.Members??new List<GroupDiagnosticsMember>()){
            if(member==null)continue;
            var peer=scopes.FirstOrDefault(s=>String.Equals(s.RemotePeerId,member.PeerId,StringComparison.OrdinalIgnoreCase));
            text.Append("\n\nMember: ").Append(Safe(member.Name,120)).Append(" (account ").Append(member.AccountId).Append(")\nRole: ").Append(Safe(member.Role,32)).Append("\nAuthorized remote device: ").Append(Safe(member.PeerId,80)).Append("\nRelationship state: ").Append(Safe(member.Relationship,32));
            text.Append("\nIndependent peer diagnostics: ").Append(peer==null?"No peer snapshot":"Available under direct conversation; not proof of GroupControl");
            text.Append("\nAuthorization scope: ").Append(String.IsNullOrEmpty(group.ServerGroupId)?"None":"GroupControl")
                .Append("\nDM permitted on this session: No\nGroup-control permitted: ").Append(String.IsNullOrEmpty(group.ServerGroupId)?"No":"Yes")
                .Append("\nGroupControl state: ").Append(Safe(member.GroupControlState,120)).Append("\nKey handoff: ").Append(Safe(member.KeyHandoffState,120));
        }
        return text.ToString();
    }
    static string Generation(long value){return value>0?value.ToString():"Unavailable";}
    static void AddScope(Dictionary<string,DiagnosticsConnectionScope> values,DiagnosticsConnectionScope scope){
        if(scope==null||String.IsNullOrEmpty(scope.RemotePeerId)&&String.IsNullOrEmpty(scope.ConversationId)&&String.IsNullOrEmpty(scope.AccountId))return;
        // Media state has a Peer ID but not a conversation/account. Fold it into
        // the already-known stable session rather than creating a second row.
        var samePeer=!String.IsNullOrEmpty(scope.RemotePeerId)?values.Values.FirstOrDefault(item=>String.Equals(item.RemotePeerId,scope.RemotePeerId,StringComparison.OrdinalIgnoreCase)):null;
        if(samePeer!=null){if(String.IsNullOrEmpty(samePeer.AccountId))samePeer.AccountId=scope.AccountId;if(String.IsNullOrEmpty(samePeer.ConversationId))samePeer.ConversationId=scope.ConversationId;return;}
        if(!values.ContainsKey(scope.Key))values.Add(scope.Key,scope);
    }
    static DiagnosticsConnectionScope ScopeFromConnectionDetails(string profile,string text){return Scope(profile,Label(text,"Account ID:"),Label(text,"Conversation ID:"),First(Label(text,"Expected Peer ID:"),Label(text,"Presented/current Peer ID:")));}
    static DiagnosticsConnectionScope ScopeFromEvent(string profile,string text){return Scope(profile,Value(text,"account"),Value(text,"conversation"),First(Value(text,"expected_peer"),Value(text,"current_peer")));}
    static DiagnosticsConnectionScope ScopeFromMedia(string profile,string text){return Scope(profile,"", "",First(Value(text,"expected_peer_id"),Value(text,"verified_peer_id")));}
    static DiagnosticsConnectionScope Scope(string profile,string account,string conversation,string peer){account=Clean(account);conversation=Clean(conversation);peer=Clean(peer);return String.IsNullOrEmpty(account)&&String.IsNullOrEmpty(conversation)&&String.IsNullOrEmpty(peer)?null:new DiagnosticsConnectionScope{Profile=profile??"",AccountId=account,ConversationId=conversation,RemotePeerId=peer};}
    static bool MatchesScope(DiagnosticsConnectionScope wanted,DiagnosticsConnectionScope actual){if(wanted==null||actual==null)return false;if(!String.IsNullOrEmpty(wanted.RemotePeerId)&&!String.IsNullOrEmpty(actual.RemotePeerId))return String.Equals(wanted.RemotePeerId,actual.RemotePeerId,StringComparison.OrdinalIgnoreCase);if(!String.IsNullOrEmpty(wanted.ConversationId)&&!String.IsNullOrEmpty(actual.ConversationId))return String.Equals(wanted.ConversationId,actual.ConversationId,StringComparison.Ordinal);return !String.IsNullOrEmpty(wanted.AccountId)&&String.Equals(wanted.AccountId,actual.AccountId,StringComparison.Ordinal);}
    static string Section(DiagnosticsSnapshot value,string name){var section=(value.Sections??new List<DiagnosticsSection>()).FirstOrDefault(item=>String.Equals(item.Name,name,StringComparison.OrdinalIgnoreCase));return section==null?"":section.Details??"";}
    static IEnumerable<string> Lines(string value){return (value??"").Replace("\r","").Split(new[]{'\n'},StringSplitOptions.RemoveEmptyEntries);}
    static string Label(string value,string label){return Lines(value).Select(line=>line.Trim()).Where(line=>line.StartsWith(label,StringComparison.OrdinalIgnoreCase)).Select(line=>line.Substring(label.Length).Trim()).FirstOrDefault()??"";}
    static string Value(string value,string key){var match=Regex.Match(value??"","(?:^|\\s)"+Regex.Escape(key)+"=([^\\s]+)",RegexOptions.IgnoreCase);return match.Success?match.Groups[1].Value:"";}
    static string First(string left,string right){return String.IsNullOrEmpty(Clean(left))?right:left;}
    static string Clean(string value){if(String.IsNullOrWhiteSpace(value)||value=="-"||value.Equals("none",StringComparison.OrdinalIgnoreCase)||value.Equals("unavailable",StringComparison.OrdinalIgnoreCase)||value.Equals("unspecified",StringComparison.OrdinalIgnoreCase)||value.Equals("not presented",StringComparison.OrdinalIgnoreCase)||value.Equals("not verified/available",StringComparison.OrdinalIgnoreCase))return "";return value.Trim();}
    static string ResolveAccountDisplayName(string accountId,Func<string,string> resolve){long id;if(resolve==null||!Int64.TryParse(accountId,out id)||id<=0)return "";try{return resolve(id.ToString())??"";}catch{return "";}}
    static string ResolvePeerDisplayLabel(string details,Func<string,string> resolve){if(resolve==null)return "";string peer=First(Value(details,"expected_peer_id"),Value(details,"verified_peer_id"));if(String.IsNullOrEmpty(peer))return "";try{return resolve(peer)??"";}catch{return "";}}
    static string PresentIdentity(string details,Func<string,string> resolver){
        if(String.IsNullOrEmpty(details)||resolver==null)return details;
        string replaced=Regex.Replace(details,@"(?im)^Account ID:\s*(\d+)\s*$",match=>{string name=ResolveAccountDisplayName(match.Groups[1].Value,resolver);return String.IsNullOrEmpty(name)?match.Value:"Account: "+name+":"+match.Groups[1].Value;});
        replaced=Regex.Replace(replaced,@"(?im)^Conversation ID:\s*account:(\d+)\s*$",match=>{string name=ResolveAccountDisplayName(match.Groups[1].Value,resolver);return String.IsNullOrEmpty(name)?match.Value:"Conversation: "+name+":"+match.Groups[1].Value;});
        return Regex.Replace(replaced,@"(?i)(\baccount=)(\d+)(\s+conversation=)account:\2\b",match=>{string name=ResolveAccountDisplayName(match.Groups[2].Value,resolver);return String.IsNullOrEmpty(name)?match.Value:match.Groups[1].Value+name+":"+match.Groups[2].Value+match.Groups[3].Value+name+":"+match.Groups[2].Value;});
    }
    static void Add(List<DiagnosticsSection> values,string name,string content){if(!String.IsNullOrWhiteSpace(content))values.Add(new DiagnosticsSection{Name=name,Details=DiagnosticsPrivacyFilter.Redact(content)});}
    static string Read(Func<string> source){try{return source==null?"":source()??"";}catch(Exception error){return "Unavailable: "+error.GetType().Name;}}
    static string Safe(string value,int limit){value=(value??"").Replace('\r',' ').Replace('\n',' ');return value.Length>limit?value.Substring(0,limit):value;}
}

public static class DiagnosticsPrivacyFilter {
    static readonly Regex SensitiveLine=new Regex("(?im)^([^\\r\\n]*(?:password|secret|token|authorization|private[_ -]?key|turn[_ -]?(?:uri|username|user)|credential[_ -]?uri|pcm|message[_ -]?content)[^=:\\r\\n]*[=:]).*$",RegexOptions.Compiled);
    public static string Redact(string value){if(String.IsNullOrEmpty(value))return "";string filtered=SensitiveLine.Replace(value,"$1 [redacted]");return filtered.Length>65536?filtered.Substring(0,65536):filtered;}
}

// Small profile-local persistence: one active marker per concurrent logical operation
// and a bounded structured failure list. Writes are atomic and only happen on transitions.
public sealed class DiagnosticsRetentionStore {
    const int MaxFailures=15,MaxActive=4,MaxSnapshotChars=16384;
    readonly string root,activePath,historyPath;
    readonly JavaScriptSerializer json=new JavaScriptSerializer{MaxJsonLength=1024*1024};
    public DiagnosticsRetentionStore(string profileDirectory){root=Path.Combine(Path.GetFullPath(profileDirectory),"Diagnostics");activePath=Path.Combine(root,"active-operations.json");historyPath=Path.Combine(root,"recent-failures.json");}
    public IList<FailureSnapshot> Failures(){try{return ReadFailures().Take(MaxFailures).Select(CloneFailure).ToList().AsReadOnly();}catch{return new List<FailureSnapshot>().AsReadOnly();}}
    public bool UpdateOperation(ActiveDiagnosticOperation operation){
        if(operation==null||String.IsNullOrWhiteSpace(operation.OperationType)||String.IsNullOrWhiteSpace(operation.OperationId))return false;
        var values=ReadActive();var prior=values.FirstOrDefault(x=>x.OperationType==operation.OperationType&&x.OperationId==operation.OperationId);
        if(prior!=null&&prior.Stage==operation.Stage&&prior.Generation==operation.Generation)return false;
        values.RemoveAll(x=>x.OperationType==operation.OperationType&&x.OperationId==operation.OperationId);
        operation.UpdatedUtc=DateTime.UtcNow.ToString("O");operation.Diagnostics=BoundSnapshot(operation.Diagnostics);values.Insert(0,CloneOperation(operation));if(values.Count>MaxActive)values=values.Take(MaxActive).ToList();WriteAtomic(activePath,json.Serialize(values));return true;
    }
    public void CompleteOperation(string operationType,string operationId){var values=ReadActive();int count=values.RemoveAll(x=>x.OperationType==operationType&&x.OperationId==operationId);if(count>0)WriteAtomic(activePath,json.Serialize(values));}
    public void CompleteAllOperations(){var values=ReadActive();if(values.Count>0)WriteAtomic(activePath,json.Serialize(new List<ActiveDiagnosticOperation>()));}
    public void AddFailure(FailureSnapshot failure){if(failure==null)return;var values=ReadFailures();values.RemoveAll(x=>x.Id==failure.Id);values.Insert(0,CloneFailure(failure));if(values.Count>MaxFailures)values=values.Take(MaxFailures).ToList();WriteAtomic(historyPath,json.Serialize(values));}
    public IList<FailureSnapshot> RecoverUncleanOperations(string build){
        var active=ReadActive();if(active.Count==0)return new List<FailureSnapshot>().AsReadOnly();var recovered=new List<FailureSnapshot>();
        foreach(var operation in active){var baseSnapshot=DiagnosticsService.Clone(operation.Diagnostics)??new DiagnosticsSnapshot{Sections=new List<DiagnosticsSection>()};baseSnapshot.TimestampUtc=operation.UpdatedUtc;baseSnapshot.Build=String.IsNullOrEmpty(baseSnapshot.Build)?build:baseSnapshot.Build;var section=baseSnapshot.Sections.FirstOrDefault(s=>s.Name=="Crash recovery");if(section==null)baseSnapshot.Sections.Add(new DiagnosticsSection{Name="Crash recovery",Details="Previous operation did not terminate cleanly.\noperation_type="+operation.OperationType+"\noperation_id="+operation.OperationId+"\nlast_stage="+operation.Stage+"\ngeneration="+operation.Generation+"\nmodule="+operation.Module+"\nlast_updated_utc="+operation.UpdatedUtc});
            var failure=new FailureSnapshot{Id="unclean-"+operation.OperationType+"-"+operation.OperationId+"-"+operation.UpdatedUtc,TimestampUtc=operation.UpdatedUtc,Title=operation.OperationType+" did not terminate cleanly",OperationType=operation.OperationType,OperationId=operation.OperationId,FailureDomain="process_exit",Reason="Previous operation did not terminate cleanly",Stage=operation.Stage,Generation=operation.Generation,Module=operation.Module,Diagnostics=baseSnapshot};AddFailure(failure);recovered.Add(CloneFailure(failure));
        }
        WriteAtomic(activePath,json.Serialize(new List<ActiveDiagnosticOperation>()));return recovered.AsReadOnly();
    }
    static DiagnosticsSnapshot BoundSnapshot(DiagnosticsSnapshot snapshot){snapshot=DiagnosticsService.Clone(snapshot);if(snapshot==null)return null;foreach(var section in snapshot.Sections){section.Details=DiagnosticsPrivacyFilter.Redact(section.Details);if(section.Details.Length>MaxSnapshotChars)section.Details=section.Details.Substring(0,MaxSnapshotChars);}return snapshot;}
    List<ActiveDiagnosticOperation> ReadActive(){try{return (json.Deserialize<List<ActiveDiagnosticOperation>>(File.ReadAllText(activePath))??new List<ActiveDiagnosticOperation>()).Take(MaxActive).ToList();}catch{return new List<ActiveDiagnosticOperation>();}}
    List<FailureSnapshot> ReadFailures(){try{return json.Deserialize<List<FailureSnapshot>>(File.ReadAllText(historyPath))??new List<FailureSnapshot>();}catch{return new List<FailureSnapshot>();}}
    static ActiveDiagnosticOperation CloneOperation(ActiveDiagnosticOperation v){return new ActiveDiagnosticOperation{OperationType=v.OperationType,OperationId=DiagnosticsPrivacyFilter.Redact(v.OperationId),Stage=DiagnosticsPrivacyFilter.Redact(v.Stage),Generation=v.Generation,Module=v.Module,UpdatedUtc=v.UpdatedUtc,Diagnostics=BoundSnapshot(v.Diagnostics)};}
    static FailureSnapshot CloneFailure(FailureSnapshot v){return new FailureSnapshot{Id=v.Id,TimestampUtc=v.TimestampUtc,Title=DiagnosticsPrivacyFilter.Redact(v.Title),OperationType=v.OperationType,OperationId=DiagnosticsPrivacyFilter.Redact(v.OperationId),FailureDomain=v.FailureDomain,Reason=DiagnosticsPrivacyFilter.Redact(v.Reason),Stage=DiagnosticsPrivacyFilter.Redact(v.Stage),Generation=v.Generation,Module=v.Module,Diagnostics=BoundSnapshot(v.Diagnostics)};}
    void WriteAtomic(string path,string value){Directory.CreateDirectory(root);string temporary=path+".tmp";File.WriteAllText(temporary,value,new System.Text.UTF8Encoding(false));try{if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);}finally{if(File.Exists(temporary))File.Delete(temporary);}}
}
}
