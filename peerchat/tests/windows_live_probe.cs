// Manual real-account interop probe. Build and launch via test-windows-live.ps1.
using System;
using System.Collections.Concurrent;
using System.Collections;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using PeerChatDesktop;

internal static class WindowsLiveProbe {
    static readonly ConcurrentQueue<DiscoveryEvent> events=new ConcurrentQueue<DiscoveryEvent>();
    static CoreAdapter core;
    static DiscoveryClient server;
    static string peer;
    static bool nativeReady;
    static string stage="arguments";
    static bool outboundQueued;
    static int nativeErrors;
    static bool mediaEnabled;
    static MediaTurnCredentials mediaTurn;
    static ulong mediaConfigurationGeneration;
    static string selectedGroupId;
    const ulong discoveryGeneration=1, networkGeneration=1;
    static readonly object relayGate=new object();
    static readonly Dictionary<ulong,RelayTransport> relays=new Dictionary<ulong,RelayTransport>();
    static readonly Dictionary<string,ServerGroupRoster> rosters=new Dictionary<string,ServerGroupRoster>(StringComparer.Ordinal);
    static readonly HashSet<string> controlRequested=new HashSet<string>(StringComparer.Ordinal);
    static DateTime nextRosterRefresh=DateTime.MinValue;
    static DiscoverySearchResult selectedFriend;
    static string selectedFriendName,expectedFriendPeer,dmMessageId,dmText,dmReplyText,dmProfile;
    static bool dmReplyReceived;
    static readonly System.Diagnostics.Stopwatch dmClock=new System.Diagnostics.Stopwatch();
    static ulong friendGeneration=1;
    static string selectedRevocationKey="";
    static DateTime nextFriendRefresh=DateTime.MinValue;
    static bool friendRouteReady,friendTrustFailed;
    static readonly HashSet<string> verifiedPeers=new HashSet<string>(StringComparer.Ordinal);
    static DeliveryTargetsData dmTargets;
    static readonly HashSet<long> pinAttempts=new HashSet<long>();
    static object mailboxExecution;
    static MethodInfo mailboxService,mailboxStop,mailboxDiagnostics;
    static PropertyInfo mailboxRegistered,mailboxNextEligibility;
    static Task mailboxTask;
    static DiscoveryClient mailboxTaskServer;
    static long mailboxTaskAccount,mailboxTaskRecipient,mailboxTaskGeneration;
    static DateTime nextMailboxService=DateTime.MinValue;
    static DateTime mailboxEligibilityDue=DateTime.MinValue;
    static bool mailboxReady;
    static string SafeFailure(Exception error){
        var rejected=error as DiscoveryRequestException;
        if(rejected!=null){
            switch(rejected.ErrorCode){
                case "remember_device_mismatch":case "remember_proof_invalid":case "remember_rate_limited":
                case "remember_revoked":case "remember_invalid":case "session_invalid":case "device_session_mismatch":
                    return rejected.ErrorCode;
                default:return "server_rejection_other";
            }
        }
        if(error is InvalidOperationException){
            switch(error.Message){
                case "Native mailbox operation rejected.":return "native_mailbox_rejected";
                case "Attachment device proof was rejected.":return "managed_proof_rejected";
                case "Attachment device session response was invalid.":return "device_session_response_invalid";
                case "Saved attachment device authorization is unavailable; it will not be replaced automatically.":return "protected_credential_unavailable";
                case "Saved attachment device authorization belongs to a different server, account, or device.":return "credential_scope_mismatch";
                case "Attachment authenticated account, profile, or server context changed.":return "account_context_changed";
                case "Attachment device verification needs your approval before first enrollment.":return "first_enrollment_denied";
                case "Selected disposable friend is not active":return "selected_friend_not_active";
                case "Selected disposable friend device does not match the expected fingerprint":return "expected_friend_peer_mismatch";
                case "Selected friend device changed; explicit verification required":return "persisted_friend_peer_changed";
                case "Selected friend account or device changed":return "selected_friend_changed_during_probe";
                case "Selected friend device is revoked":return "selected_friend_revoked";
                case "Selected friend revocation state changed during probe":return "selected_revocation_changed_during_probe";
                case "Authenticated friend binding rejected":return "native_friend_binding_rejected";
            }
        }
        return "unclassified";
    }
    static DiscoveryClient OpenScopedSession(DiscoveryClient passwordSession,long account,string profile,bool allowFirstEnrollment){
        // Select the production internal overload that accepts a native proof callback.
        // Reflection only exposes the callback boundary for stage diagnostics; all proof,
        // challenge, persistence, and scoped-session checks remain in production code.
        var method=typeof(AttachmentDeviceSession).GetMethods(BindingFlags.Static|BindingFlags.NonPublic)
            .Single(m=>m.Name=="Open"&&m.GetParameters().Length==7&&m.GetParameters()[1].ParameterType==typeof(Func<byte[],byte[]>));
        string token=passwordSession.SessionToken;
        Func<byte[],byte[]> sign=input=>{stage="device_session_native_proof";var result=core.Mailbox(9,input);stage="device_session_proof_signed";return result;};
        Func<bool> current=()=>passwordSession.AccountId==account&&passwordSession.SessionToken==token;
        try{return (DiscoveryClient)method.Invoke(null,new object[]{passwordSession,sign,account,peer,profile,allowFirstEnrollment,current});}
        catch(TargetInvocationException error){throw error.InnerException??error;}
    }
    static bool Hex(string s,int n){return s!=null&&s.Length==n&&s.All(c=>Uri.IsHexDigit(c));}
    static void Check(bool ok,string message){if(!ok)throw new InvalidOperationException(message);}
    static string ReadPassword(){
        if(Console.IsInputRedirected){string line=Console.ReadLine();Check(!String.IsNullOrEmpty(line),"Password input missing");return line;}
        Console.Write("Password (not echoed): ");var chars=new StringBuilder();
        while(true){var key=Console.ReadKey(true);if(key.Key==ConsoleKey.Enter)break;if(key.Key==ConsoleKey.Backspace){if(chars.Length>0)chars.Length--;continue;}if(!Char.IsControl(key.KeyChar))chars.Append(key.KeyChar);}
        Console.WriteLine();string result=chars.ToString();chars.Clear();return result;
    }
    static string ControlKey(ServerGroupRoster roster,string target){return roster.GroupId+":"+roster.RosterGeneration+":"+target;}
    static bool Selected(string group){return !String.IsNullOrEmpty(selectedGroupId)&&group==selectedGroupId;}
    static void InvalidateGroupAuthority(){
        StopRelays();core.ClearAuthenticatedGroupRosters();rosters.Clear();controlRequested.Clear();
    }
    static void RefreshGroups(){
        if(String.IsNullOrEmpty(selectedGroupId)){nextRosterRefresh=DateTime.UtcNow.AddSeconds(5);return;}
        try{
            // Only the explicitly selected test group may create peer controls.
            var roster=server.GroupState(peer).SingleOrDefault(r=>r!=null&&(r.GroupId==selectedGroupId||r.LegacyGroupId==selectedGroupId));
            if(roster==null||String.IsNullOrEmpty(roster.GroupId)){InvalidateGroupAuthority();nextRosterRefresh=DateTime.UtcNow.AddSeconds(5);return;}
            ServerGroupRoster previous;
            if(rosters.TryGetValue(roster.GroupId,out previous)&&previous.RosterGeneration!=roster.RosterGeneration)InvalidateGroupAuthority();
            if(!core.SetAuthenticatedGroupRoster(server.AccountId,discoveryGeneration,roster))throw new InvalidOperationException("Selected signed roster binding failed.");
            rosters.Clear();rosters[roster.GroupId]=roster;
            var allowed=new HashSet<string>(StringComparer.Ordinal);
            if(roster.Members==null||!roster.Members.Any(m=>m.AccountId==server.AccountId&&m.PeerId==peer&&m.State=="member"))throw new InvalidOperationException("Selected group membership is not active.");
            bool distributor=roster.DistributorPeerId==peer;
            foreach(var remote in roster.Members.Where(m=>m.State=="member"&&m.AccountId!=server.AccountId&&m.PeerId!=peer&&(distributor||m.PeerId==roster.DistributorPeerId))){
                var key=ControlKey(roster,remote.PeerId);allowed.Add(key);
                if(controlRequested.Add(key)){
                    if(!core.BeginGroupControl(roster.GroupId,remote.AccountId,remote.PeerId,discoveryGeneration))controlRequested.Remove(key);
                    else Console.WriteLine("group_control_requested group="+roster.GroupId+" generation="+roster.RosterGeneration+(mediaEnabled?"":" target="+remote.PeerId));
                }
            }
            controlRequested.RemoveWhere(key=>!allowed.Contains(key));
            nextRosterRefresh=DateTime.UtcNow.AddSeconds(5);
        }catch{InvalidateGroupAuthority();throw;}
    }
    static void StartGroupRelay(CoreEvent request){
        ulong attempt=request.attempt>0?(ulong)request.attempt:0;
        ServerGroupRoster roster=null;
        bool authorized=attempt>0&&server!=null&&request.networkGeneration==(long)networkGeneration&&
            !String.IsNullOrEmpty(server.SessionToken)&&!String.IsNullOrEmpty(request.authorizationGroupId)&&
            (Selected(request.authorizationGroupId)||rosters.ContainsKey(request.authorizationGroupId))&&
            rosters.TryGetValue(request.authorizationGroupId,out roster)&&roster.RosterGeneration==request.authorizationRosterGeneration&&
            roster.Members.Any(m=>m.AccountId==server.AccountId&&m.PeerId==peer&&m.State=="member")&&
            roster.Members.Any(m=>m.AccountId==request.authorizationTargetAccountId&&m.PeerId==request.expected&&m.State=="member");
        if(!authorized){if(attempt>0)core.RelayClosed(attempt,"Group-control request is stale or outside the authenticated roster.");return;}
        try{
            var relay=new RelayTransport(server,core,attempt,peer,request.authorizationTargetAccountId,request.expected,value=>{
                lock(relayGate)relays.Remove(value);
                // A completed handoff does not need a fresh request each pulse.
            },roster.GroupId,roster.RosterGeneration);
            lock(relayGate){if(relays.ContainsKey(attempt)){relay.Dispose();return;}relays.Add(attempt,relay);}
            relay.Start();Console.WriteLine("group_relay_started group="+roster.GroupId+" generation="+roster.RosterGeneration+(mediaEnabled?"":" target="+request.expected));
        }catch(Exception error){core.RelayClosed(attempt,"Group-control relay startup failed: "+error.GetType().Name);}
    }
    static void StopRelays(){RelayTransport[] active;lock(relayGate){active=relays.Values.ToArray();relays.Clear();}foreach(var relay in active)try{relay.Dispose();}catch{}}
    static void StartMailbox(){
        if(mailboxExecution!=null)return;
        Check(core.MailboxSupported,"Packaged native mailbox is unavailable");
        Check(!File.Exists(Path.Combine(dmProfile,"mailbox-policy.dat")),"Isolated profile has an explicit mailbox policy; probe will not override it");
        var type=typeof(CoreAdapter).Assembly.GetType("PeerChatDesktop.MailboxExecution",true);
        var constructor=type.GetConstructor(BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic,null,new[]{typeof(CoreAdapter)},null);
        mailboxService=type.GetMethods(BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic).Single(m=>m.Name=="Service"&&m.GetParameters().Length==9);
        mailboxStop=type.GetMethod("Stop",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic);
        mailboxDiagnostics=type.GetMethod("DiagnosticsSnapshot",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic);
        mailboxRegistered=type.GetProperty("RegistrationAccepted",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic);
        mailboxNextEligibility=type.GetProperty("NextEligibilityUtc",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic);
        Check(constructor!=null&&mailboxStop!=null&&mailboxDiagnostics!=null&&mailboxRegistered!=null&&mailboxNextEligibility!=null,"Production mailbox adapter shape changed");
        mailboxExecution=constructor.Invoke(new object[]{core});nextMailboxService=DateTime.MinValue;
    }
    static void PumpMailbox(){
        if(mailboxExecution==null||selectedFriend==null||friendTrustFailed)return;
        if(mailboxEligibilityDue<=DateTime.UtcNow)mailboxReady=false;
        if(mailboxTask!=null){
            if(!mailboxTask.IsCompleted)return;
            bool succeeded=!mailboxTask.IsFaulted&&!mailboxTask.IsCanceled;
            bool current=mailboxTaskServer==server&&mailboxTaskAccount==server.AccountId&&
                selectedFriend!=null&&mailboxTaskRecipient==selectedFriend.AccountId&&mailboxTaskGeneration==(long)friendGeneration;
            if(!current){mailboxReady=false;mailboxEligibilityDue=DateTime.MinValue;mailboxTask=null;nextMailboxService=DateTime.UtcNow.AddSeconds(2);return;}
            if(succeeded){
                bool registered=(bool)mailboxRegistered.GetValue(mailboxExecution,null);
                string diagnostics=(string)mailboxDiagnostics.Invoke(mailboxExecution,null);
                string marker="Eligibility last outcome: recipient="+selectedFriend.AccountId+" installed";
                DateTime due=(DateTime)mailboxNextEligibility.GetValue(mailboxExecution,null);
                mailboxEligibilityDue=due;
                mailboxReady=registered&&due>DateTime.UtcNow&&diagnostics.IndexOf(marker,StringComparison.Ordinal)>=0;
                nextMailboxService=due>DateTime.UtcNow&&due<DateTime.UtcNow.AddSeconds(10)?due:DateTime.UtcNow.AddSeconds(mailboxReady?5:2);
                Console.WriteLine("dm_mailbox registration="+registered+" selected_eligibility="+mailboxReady);
            }else{mailboxReady=false;mailboxEligibilityDue=DateTime.MinValue;nextMailboxService=DateTime.UtcNow.AddSeconds(5);Console.WriteLine("dm_mailbox service_failed=true");}
            mailboxTask=null;
        }
        if(DateTime.UtcNow<nextMailboxService)return;
        var scoped=server;var execution=mailboxExecution;long account=scoped.AccountId,recipient=selectedFriend.AccountId,generation=checked((long)friendGeneration);string device=peer;
        mailboxTaskServer=scoped;mailboxTaskAccount=account;mailboxTaskRecipient=recipient;mailboxTaskGeneration=generation;
        mailboxTask=Task.Run(()=>{
            if(scoped!=server||selectedFriend==null||selectedFriend.AccountId!=recipient||friendGeneration!=(ulong)generation)throw new InvalidOperationException("Mailbox scope changed");
            Action<string,bool> acknowledged=(sender,inserted)=>{if(sender==expectedFriendPeer&&inserted)core.History(sender,Int64.MaxValue);};
            try{mailboxService.Invoke(execution,new object[]{scoped,device,true,acknowledged,null,null,new long[]{recipient},generation,account});}
            catch(TargetInvocationException error){throw error.InnerException??error;}
        });
        nextMailboxService=DateTime.UtcNow.AddSeconds(5);
    }
    static void StopMailbox(){
        if(mailboxExecution==null)return;
        try{mailboxStop.Invoke(mailboxExecution,null);}catch{}
        try{
            if(mailboxTask!=null&&!mailboxTask.Wait(TimeSpan.FromSeconds(15))){
                if(server!=null)server.CloseRpc();
                if(!mailboxTask.Wait(TimeSpan.FromSeconds(5)))Console.WriteLine("dm_mailbox drain_timeout=true");
            }
        }catch{}
        mailboxTask=null;mailboxExecution=null;mailboxReady=false;mailboxEligibilityDue=DateTime.MinValue;
    }
    static void ClearFriendAuthority(){
        friendRouteReady=false;selectedFriend=null;
        core.SetAuthenticatedFriends(server.AccountId,++friendGeneration,new FriendConversationBinding[0]);
        StopMailbox();StopRelays();
    }
    static void RefreshFriend(string profile){
        bool previouslySelected=selectedFriend!=null;
        try{
            var state=server.GetFriendState();
            var friend=state.Friends.SingleOrDefault(f=>f.Username==selectedFriendName&&f.AccountId>0&&Hex(f.PublicIdentity,64));
            Check(friend!=null&&friend.Username!=server.Username,"Selected disposable friend is not active");
            // A deliberate Android reconnect can temporarily select another
            // retained device. Clear authority and wait; never verify or send to it.
            if(friend.PublicIdentity!=expectedFriendPeer){ClearFriendAuthority();nextFriendRefresh=DateTime.UtcNow.AddMilliseconds(250);return;}
            // Other friendships do not change this DM's scope. Only the selected
            // binding is installed below. Production native mailbox receive
            // rejects unmapped senders before import/ACK; never filter or forge
            // authenticated server responses to make an isolation test pass.
            var index=new FriendConversationIndex(profile);
            var existing=index.Records.SingleOrDefault(x=>x.AccountId==friend.AccountId);
            Check(existing==null||String.IsNullOrEmpty(existing.CurrentPeerId)||existing.CurrentPeerId==friend.PublicIdentity,"Selected friend device changed; explicit verification required");
            Check(selectedFriend==null||selectedFriend.AccountId==friend.AccountId&&selectedFriend.PublicIdentity==friend.PublicIdentity,"Selected friend account or device changed");
            if(selectedFriend==null)index.Reconcile(new[]{friend},state.DeviceNames);
            var binding=index.Records.Single(x=>x.AccountId==friend.AccountId);
            List<string> revoked;binding.RevokedPeerIds=state.RevokedDevices.TryGetValue(friend.AccountId,out revoked)?new List<string>(revoked):new List<string>();
            Check(!binding.RevokedPeerIds.Contains(friend.PublicIdentity),"Selected friend device is revoked");
            string revocationKey=String.Join(",",binding.RevokedPeerIds.OrderBy(x=>x,StringComparer.Ordinal));
            Check(selectedFriend==null||selectedRevocationKey==revocationKey,"Selected friend revocation state changed during probe");
            if(selectedFriend==null||selectedRevocationKey!=revocationKey){
                Check(core.SetAuthenticatedFriends(server.AccountId,++friendGeneration,new[]{binding}),"Authenticated friend binding rejected");
                selectedRevocationKey=revocationKey;
            }
            if(selectedFriend==null)core.UpdateCandidates(friend.PublicIdentity,"",0,"",0,true,networkGeneration,true);
            selectedFriend=friend;
            if(mailboxExecution==null)StartMailbox();
            nextFriendRefresh=DateTime.UtcNow.AddSeconds(5);
        }catch{if(previouslySelected)friendTrustFailed=true;ClearFriendAuthority();nextFriendRefresh=DateTime.UtcNow.AddSeconds(5);throw;}
    }
    static void StartFriendRelay(CoreEvent request){
        ulong attempt=request.attempt>0?(ulong)request.attempt:0;
        bool authorized=attempt>0&&selectedFriend!=null&&!friendTrustFailed&&request.networkGeneration==(long)networkGeneration&&
            String.IsNullOrEmpty(request.authorizationGroupId)&&String.IsNullOrEmpty(request.authorizationArchive)&&
            request.expected==selectedFriend.PublicIdentity&&!String.IsNullOrEmpty(server.SessionToken);
        if(!authorized){if(attempt>0)core.RelayClosed(attempt,"Selected friend relay scope invalid.");return;}
        try{
            var relay=new RelayTransport(server,core,attempt,peer,selectedFriend.AccountId,selectedFriend.PublicIdentity,value=>{lock(relayGate)relays.Remove(value);});
            lock(relayGate){if(relays.ContainsKey(attempt)){relay.Dispose();return;}relays.Add(attempt,relay);}
            relay.Start();Console.WriteLine("dm_relay_started target="+selectedFriend.PublicIdentity);
        }catch(Exception error){core.RelayClosed(attempt,"Selected friend relay startup failed: "+error.GetType().Name);}
    }
    static void ObserveDm(CoreEvent e){
        if(selectedFriend==null)return;
        if(e.kind==1&&e.contacts!=null)foreach(var contact in e.contacts)if(contact.verified)verifiedPeers.Add(contact.peer);
        if(e.kind==2&&(e.expected==selectedFriend.PublicIdentity||String.IsNullOrEmpty(e.expected)&&e.peer==selectedFriend.PublicIdentity)){
            if(e.attempt<=0||e.peer!=selectedFriend.PublicIdentity||pinAttempts.Contains(e.attempt)||
                verifiedPeers.Contains(selectedFriend.PublicIdentity)||friendTrustFailed){friendTrustFailed=true;return;}
            pinAttempts.Add(e.attempt);
            if(!core.PinInitialAuthenticatedFriendIdentity(e.peer,(ulong)e.attempt,friendGeneration))friendTrustFailed=true;
            else Console.WriteLine("dm_initial_native_pin_requested target="+e.peer);
        }
        if(e.kind==3&&e.peer==selectedFriend.PublicIdentity&&(String.IsNullOrEmpty(e.expected)||e.expected==selectedFriend.PublicIdentity)){friendRouteReady=true;Console.WriteLine("dm_verified_route peer="+e.peer);}
        if(e.kind==4&&e.peer==selectedFriend.PublicIdentity)friendRouteReady=false;
        if((e.kind==5||e.kind==7)&&e.messages!=null)foreach(var message in e.messages){
            if(message.sender==selectedFriend.PublicIdentity&&e.kind==5&&message.text==dmReplyText){dmReplyReceived=true;Console.WriteLine("dm_received_reply elapsed_ms="+dmClock.ElapsedMilliseconds+" id="+message.id+" sender="+message.sender);}
            if(message.sender==peer&&(dmMessageId==message.id||dmMessageId==null&&message.text==dmText)){
                if(dmMessageId==null)dmMessageId=message.id;
                dmTargets=message.deliveryTargets;
                Console.WriteLine("dm_delivery elapsed_ms="+dmClock.ElapsedMilliseconds+" id="+dmMessageId+" aggregate="+message.delivery+" selection_unresolved="+(dmTargets==null?"unknown":dmTargets.selectionUnresolved.ToString())+
                    " targets="+(dmTargets==null?"unknown":String.Join(",",(dmTargets.targets??new DeliveryTargetData[0]).Select(t=>t.device+":"+t.status))));
            }
        }
    }
    static async Task<GroupVoiceMediaConfiguration> MediaConfiguration(){
        if(mediaTurn==null||mediaTurn.ExpiresUtc<=DateTimeOffset.UtcNow.ToUnixTimeSeconds()+60){
            var scoped=server;
            var issued=await Task.Run(()=>scoped.GetMediaTurnCredentials(peer));
            Check(scoped==server&&issued!=null&&issued.ExpiresUtc>DateTimeOffset.UtcNow.ToUnixTimeSeconds()+30,"Media configuration scope or expiry invalid");
            ulong next=++mediaConfigurationGeneration;
            Check(core.SetMediaIceConfiguration(issued,1,discoveryGeneration,next),"Native media configuration rejected");
            mediaTurn=issued;
        }
        return new GroupVoiceMediaConfiguration(mediaTurn,1,discoveryGeneration,mediaConfigurationGeneration);
    }
    static bool MediaEvidence(GroupVoiceService voice,out bool received){
        received=false;string raw=voice.Metrics(selectedGroupId);
        if(String.IsNullOrEmpty(raw)||raw.Length>262144)return false;
        try{
            var root=new JavaScriptSerializer{MaxJsonLength=262144}.Deserialize<Dictionary<string,object>>(raw);
            object value;if(root==null||!root.TryGetValue("routes",out value))return false;
            bool connected=false;
            foreach(var item in (value as IEnumerable)??new object[0]){
                var route=item as Dictionary<string,object>;if(route==null)continue;
                object flag,count;
                if(route.TryGetValue("connected",out flag)&&Object.Equals(flag,true))connected=true;
                if(route.TryGetValue("receivedPackets",out count)&&Convert.ToUInt64(count)>0)received=true;
            }
            return connected;
        }catch{return false;}
    }
    static void Pump(){
        if(server!=null&&DateTime.UtcNow>=nextRosterRefresh){try{RefreshGroups();}catch(Exception error){nextRosterRefresh=DateTime.UtcNow.AddSeconds(5);Console.WriteLine("group_roster_refresh_failed type="+error.GetType().Name);}}
        if(server!=null&&selectedFriendName!=null&&DateTime.UtcNow>=nextFriendRefresh){try{RefreshFriend(dmProfile);}catch(Exception error){Console.WriteLine("dm_friend_refresh_failed type="+error.GetType().Name);}}
        if(selectedFriendName!=null)PumpMailbox();
        DiscoveryEvent n;
        while(events.TryDequeue(out n)){
            if(selectedFriendName!=null&&n.Event=="relay_demand"&&n.Purpose=="friend"){
                if(selectedFriend!=null&&n.AccountId==selectedFriend.AccountId&&n.PublicIdentity==selectedFriend.PublicIdentity&&n.TargetPublicIdentity==peer){
                    try{RefreshFriend(dmProfile);if(selectedFriend!=null)core.UpdateCandidates(selectedFriend.PublicIdentity,"",0,"",0,true,networkGeneration,true);}catch(Exception error){Console.WriteLine("dm_demand_rejected type="+error.GetType().Name);}
                }
            }
            if(selectedFriendName!=null&&(n.Event=="friendship_changed"||n.Event=="presence_changed"||n.Event=="channel_connected"))nextFriendRefresh=DateTime.MinValue;
            if(n.Event=="group_roster_changed"&&Selected(n.GroupId))nextRosterRefresh=DateTime.MinValue;
            else if(n.Event=="relay_demand"&&n.Purpose=="group_key"){
                if(n.GroupId!=null&&(Selected(n.GroupId)||rosters.ContainsKey(n.GroupId)))try{RefreshGroups();ServerGroupRoster roster;if(rosters.TryGetValue(n.GroupId,out roster)&&GroupRegistrationPolicy.IncomingControlReady(roster,n,server.AccountId,peer))core.BeginGroupControl(roster.GroupId,n.AccountId,n.PublicIdentity,discoveryGeneration);}catch(Exception error){Console.WriteLine("group_control_demand_failed type="+error.GetType().Name);}
            }
            else if(n.Event=="group_message"&&rosters.ContainsKey(n.GroupId??"")){
                bool admitted=false;try{byte[] wire=Convert.FromBase64String(n.Envelope??"");admitted=wire.Length<=65790&&core.ReceiveGroupCiphertext(wire);}catch(FormatException){}
                if(!admitted)server.QueueGroupAck(n.GroupId,n.MessageId,"rejected");
                Console.WriteLine("group_message id="+n.MessageId+" admitted="+admitted);
            }else if(n.Event=="group_delivery"&&rosters.ContainsKey(n.GroupId??"")){
                uint stage=n.State=="recipient_acknowledged"?2u:n.State=="server_accepted_online_only"||n.State=="duplicate_server_request"?1u:0u;
                core.GroupMessageStatus(n.GroupId,n.MessageId,n.State,stage);
                Console.WriteLine("group_delivery id="+n.MessageId+" state="+n.State+" accepted="+n.AcceptedCount+" duplicate="+n.DuplicateCount+" rejected="+n.RejectedCount);
            }else if((rosters.ContainsKey(n.GroupId??"")&&(n.Event=="group_room_result"||n.Event=="group_room_snapshot"))||n.Event=="channel_connected"||n.Event=="channel_disconnected")
                Console.WriteLine("event="+n.Event+" group="+(n.GroupId??"")+" state="+(n.State??""));
        }
        foreach(var e in core.Poll()){
            if(selectedFriendName!=null)ObserveDm(e);
            if(e.kind==20&&rosters.ContainsKey(e.expected??"")){bool queued=server.QueueGroupMessage(e.expected,e.text,e.attempt,e.detail);outboundQueued|=queued;if(!queued)core.GroupMessageStatus(e.expected,e.text,"Not sent: authenticated server channel unavailable");Console.WriteLine("native_group_send id="+e.text+" queued="+queued);}
            else if(e.kind==21&&rosters.ContainsKey(e.expected??"")){bool queued=server.QueueGroupAck(e.expected,e.detail,e.text);Console.WriteLine("native_group_ack id="+e.detail+" state="+e.text+" queued="+queued);}
            else if(e.kind==8){nativeErrors++;Console.WriteLine("native_error kind=8");}
            else if(e.kind==18&&server!=null){if(!String.IsNullOrEmpty(e.authorizationGroupId))StartGroupRelay(e);else if(selectedFriendName!=null)StartFriendRelay(e);else if(e.attempt>0)core.RelayClosed((ulong)e.attempt,"Relay outside selected probe scope.");}
            else if(e.kind==0){Check(e.peer==peer,"Native startup identity differs from isolated profile");nativeReady=true;}
        }
    }
    static void Until(Func<bool> predicate,string label,int seconds=20){DateTime end=DateTime.UtcNow.AddSeconds(seconds);while(DateTime.UtcNow<end){Pump();if(predicate())return;Thread.Sleep(50);}throw new TimeoutException(label+" timed out");}
    static ServerGroupRoster Roster(string id){
        var r=server.GroupState(peer).FirstOrDefault(x=>x.GroupId==id||x.LegacyGroupId==id);
        Check(r!=null,"Group absent from authenticated account roster");
        Check(r.Members.Any(m=>m.AccountId==server.AccountId&&m.PeerId==peer&&m.State=="member"),"Device is not an active roster member");
        Check(core.SetAuthenticatedGroupRoster(server.AccountId,discoveryGeneration,r),"Native signed roster validation failed");selectedGroupId=r.GroupId;return r;
    }
    static void CreateGroup(string name,string invite){
        Check(name.Length>0&&name.Length<=80,"Group name must be 1 to 80 characters");stage="group_preflight";server.GroupState(peer);
        stage="group_create_native";string legacy=core.CreateGroup(name);Check(Hex(legacy,32),"Native group creation failed");
        stage="group_creation_proof";string proof=core.GroupCreationProof(legacy);Check(!String.IsNullOrEmpty(proof),"Signed creation proof unavailable");
        stage="group_register";var roster=server.RegisterNewGroup(proof,peer);Check(roster.LegacyGroupId==legacy,"Server roster does not match native group");
        selectedGroupId=roster.GroupId;
        stage="group_roster_bind";
        Check(core.SetAuthenticatedGroupRoster(server.AccountId,1,roster),"Native signed roster validation failed");
        Console.WriteLine("group_registered id="+roster.GroupId+" legacy="+legacy+" generation="+roster.RosterGeneration);
        if(invite!=null){stage="group_invite_lookup";Check(invite=="codex"||invite=="codex1","Only disposable accounts may be invited");Check(invite!=server.Username,"Cannot invite self");var target=server.SearchPeople(invite).SingleOrDefault(x=>x.Username==invite);Check(target!=null,"Exact invite account absent");stage="group_invite";var result=server.InviteAccountToGroup(roster.GroupId,roster.RosterGeneration,target.AccountId,peer);Console.WriteLine("group_invite target="+invite+" state="+(result.ContainsKey("state")?Convert.ToString(result["state"]):"submitted"));}
    }
    static void Room(string id,string name,bool join,int holdSeconds){
        var roster=Roster(id);Until(()=>server.EventChannelConnected,"event channel");
        Check(server.QueueGroupRoom(roster.GroupId,"snapshot"),"Room snapshot refused");Until(()=>server.GroupRooms(roster.GroupId)!=null,"room snapshot");
        var room=server.GroupRooms(roster.GroupId).Rooms.FirstOrDefault(x=>x.Kind=="voice"&&x.Name==name);
        if(room==null){
            Check(server.QueueGroupRoom(roster.GroupId,"create","","voice",name),"Voice room creation refused");
            Until(()=>server.GroupRoomResult(roster.GroupId)!="Pending: create","room creation result");
            Check(server.GroupRoomResult(roster.GroupId)=="accepted","Room creation rejected: "+server.GroupRoomResult(roster.GroupId));
            room=server.GroupRooms(roster.GroupId).Rooms.FirstOrDefault(x=>x.Kind=="voice"&&x.Name==name);
        }
        Check(room!=null,"Named voice room absent from authoritative snapshot");
        Console.WriteLine("voice_room group="+roster.GroupId+" room="+room.Id);
        GroupVoiceService voice=null;bool connectedSeen=false,receivedSeen=false,mediaFailure=false;
        try{
            if(mediaEnabled){
                Check(File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"PeerChat.AudioReceive.dll")),"Packaged audio receive backend missing");
                voice=new GroupVoiceService(()=>server,()=>peer,MediaConfiguration,
                    ()=>new VoicePreferenceData{InputVolume=0,OutputVolume=0,AudioProcessingEnabled=false,
                        NoiseSuppressionEnabled=false,AutomaticGainEnabled=false,EchoCancellationEnabled=false},()=>false,
                    Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"PeerChat.AudioReceive.dll"));
                voice.Failed+=failure=>{mediaFailure=true;};
            }
            if(join){
                bool queued=voice!=null?voice.Join(roster.GroupId,room.Id).GetAwaiter().GetResult():server.QueueGroupRoom(roster.GroupId,"join_voice",room.Id);
                Check(queued,"Voice join refused");Until(()=>server.GroupRoomResult(roster.GroupId)!="Pending: join_voice","voice join result");
                Check(server.GroupRoomResult(roster.GroupId)=="accepted","Voice join rejected");Console.WriteLine("voice_join accepted room="+room.Id);
            }
            long revision=-1;DateTime end=DateTime.UtcNow.AddSeconds(holdSeconds);
            while(DateTime.UtcNow<end){
                Pump();if(voice!=null){voice.Pump().GetAwaiter().GetResult();bool received;if(MediaEvidence(voice,out received))connectedSeen=true;if(received)receivedSeen=true;}
                var state=server.GroupRooms(roster.GroupId);
                if(state!=null&&state.Revision!=revision){revision=state.Revision;var current=state.Rooms.FirstOrDefault(x=>x.Id==room.Id);if(current!=null)Console.WriteLine("voice_snapshot group="+roster.GroupId+" room="+room.Id+" revision="+revision+" members="+(mediaEnabled?current.Members.Count.ToString():String.Join(",",current.Members.Select(x=>x.AccountId+":"+x.PeerId))));}
                Thread.Sleep(50);
            }
            if(voice!=null){Console.WriteLine("media_evidence group="+roster.GroupId+" room="+room.Id+" connected_route="+connectedSeen+" received_packets="+receivedSeen+" failure="+mediaFailure);Check(connectedSeen&&receivedSeen&&!mediaFailure,"Media route or receive evidence missing");}
        }finally{if(voice!=null){try{voice.Leave().GetAwaiter().GetResult();}catch{}voice.Dispose();}}
    }
    static void Dm(string probeId,int holdSeconds){
        stage="dm_friend_snapshot";RefreshFriend(dmProfile);
        stage="dm_mailbox_initialize";StartMailbox();
        stage="dm_verified_route_and_eligibility";Until(()=>friendRouteReady&&mailboxReady||friendTrustFailed,"selected verified friend route and signed eligibility",60);
        Check(!friendTrustFailed&&friendRouteReady&&mailboxReady&&selectedFriend!=null,"Selected peer identity or signed mailbox eligibility unavailable");
        for(int round=0;round<8;round++){
        dmMessageId=null;dmTargets=null;dmReplyReceived=false;
        Console.WriteLine("dm_round round="+round);
        stage="dm_native_send";dmText="PC-LIVE-DM-v1:"+probeId+":win-to-android:"+round;
        dmReplyText="PC-LIVE-DM-v1:"+probeId+":android-to-win:"+round;
        Until(()=>selectedFriend!=null&&friendRouteReady&&mailboxReady||friendTrustFailed,"reconnected selected device",60);
        dmClock.Restart();
        Check(!friendTrustFailed&&selectedFriend!=null&&core.Send(selectedFriend.PublicIdentity,dmText),"Native durable direct message enqueue failed");
        Console.WriteLine("dm_local_queued probe="+probeId+" selected_peer="+selectedFriend.PublicIdentity);
        stage="dm_wait_receipts";DateTime end=DateTime.UtcNow.AddSeconds(holdSeconds);
        DateTime completeAt=DateTime.MinValue;
        while(DateTime.UtcNow<end){
            Pump();if(friendTrustFailed)break;
            bool selectedReceived=dmTargets!=null&&dmTargets.targets!=null&&selectedFriend!=null&&
                dmTargets.targets.Any(t=>t.device==selectedFriend.PublicIdentity&&t.status==1);
            if(dmMessageId!=null&&selectedReceived&&dmReplyReceived){
                if(completeAt==DateTime.MinValue)completeAt=DateTime.UtcNow;
                if(DateTime.UtcNow-completeAt>=TimeSpan.FromSeconds(5))break;
            }
            Thread.Sleep(50);
        }
        bool acknowledged=dmTargets!=null&&dmTargets.targets!=null&&selectedFriend!=null&&
            dmTargets.targets.Any(t=>t.device==selectedFriend.PublicIdentity&&t.status==1);
        Console.WriteLine("dm_evidence probe="+probeId+" local_message="+(dmMessageId!=null)+" selected_ack="+acknowledged+
            " reply_received="+dmReplyReceived+" selection_unresolved="+(dmTargets==null?"unknown":dmTargets.selectionUnresolved.ToString())+
            " target_statuses="+(dmTargets==null?"unknown":String.Join(",",(dmTargets.targets??new DeliveryTargetData[0]).Select(t=>t.device+":"+t.status))));
        Check(!friendTrustFailed&&dmMessageId!=null&&acknowledged&&dmReplyReceived,"Selected verified DM receipt or reply missing; account-wide delivery not asserted");
        }
    }
    static int Main(string[] args){
        // host port pinned-sha256 existing-profile username command [command args]
        if(args.Length<6){Console.Error.WriteLine("Usage: <host> <port> <sha256-pin> <existing-profile> <codex|codex1> <status|create-group|room|send|dm> [args]");return 2;}
        try{
            string host=args[0],pin=args[2],profile=Path.GetFullPath(args[3]),user=args[4],command=args[5];int port=Int32.Parse(args[1]);
            Check(port>0&&port<=65535&&Hex(pin,64),"Valid port and full SHA-256 TLS pin required");
            Check(user=="codex"||user=="codex1","Only disposable codex/codex1 accounts allowed");
            Check(Directory.Exists(profile)&&File.Exists(Path.Combine(profile,"windows-live-probe.marker")),"Profile must be an isolated probe directory");
            Check(new[]{"status","create-group","room","send","dm"}.Contains(command),"Unknown command");
            if(command=="room"||command=="send"){Check(args.Length>6&&Hex(args[6],32),"Selected group ID must be a 32-character hex ID");selectedGroupId=args[6];}
            if(command=="room")mediaEnabled=args.Length==11&&args[10]=="media";
            if(command=="dm"){
                Check(args.Length==10&&new[]{"codex","codex1"}.Contains(args[6])&&args[6]!=user,"DM target must be the other disposable account");
                Check(Hex(args[7],32)&&args[7]==args[7].ToLowerInvariant(),"Probe ID must be 32 lowercase hex characters");
                int hold=Int32.Parse(args[8]);Check(hold>=30&&hold<=600,"DM hold must be 30 to 600 seconds");
                Check(Hex(args[9],64)&&args[9]==args[9].ToLowerInvariant(),"Expected recipient fingerprint must be 64 lowercase hex characters");
                selectedFriendName=args[6];dmProfile=profile;expectedFriendPeer=args[9];
            }
            stage="native_core_load";
            using(core=new CoreAdapter()){
                stage="profile_identity";peer=core.ProfilePeerId(profile);Check(Hex(peer,64),"Isolated profile identity creation failed");
                stage="native_start";core.Start(profile,0,"Windows live probe");
                stage="native_ready";Until(()=>nativeReady,"native profile startup");
                var passwordSession=new DiscoveryClient(host,port,pin); // No trust callback: changed certificate fails closed.
                try{
                    stage="password_input";string password=ReadPassword();
                    stage="password_login";passwordSession.Login(user,password);password=null;Check(passwordSession.Username==user,"Authenticated account mismatch");
                    stage="account_presence";var presence=passwordSession.ReportPresence(peer,directCandidates:false);Check(presence.AccountId==passwordSession.AccountId&&presence.Username==user&&presence.RegisteredPublicIdentity==peer,"Device presence rejected");
                    // Native remembered-device proof requires both the profile's linked
                    // account and the authenticated friend snapshot to name this account.
                    stage="native_account_bind";Check(core.SetGroupAccount(passwordSession.AccountId,user),"Native account binding failed");
                    core.SetNetworkContext(networkGeneration,"");
                    stage="native_friend_scope";Check(core.SetAuthenticatedFriends(passwordSession.AccountId,1,new FriendConversationBinding[0]),"Native authenticated account scope failed");
                    // Production signed enrollment/resume. Its credential is DPAPI-protected in this isolated profile.
                    // All subsequent group and room calls use the resumed, device-bound session.
                    stage="device_session_credential_check";
                    bool allowFirstEnrollment=!File.Exists(Path.Combine(profile,"windows-live-enrolled.marker"))&&!AttachmentDeviceSession.HasCredential(passwordSession,passwordSession.AccountId,peer,profile);
                    stage=allowFirstEnrollment?"device_session_enroll":"device_session_resume";
                    server=OpenScopedSession(passwordSession,passwordSession.AccountId,profile,allowFirstEnrollment);
                    Check(server.MatchesAttachmentDeviceSession(passwordSession.AccountId,peer),"Device-bound session was not established");
                    File.WriteAllText(Path.Combine(profile,"windows-live-enrolled.marker"),"Device session enrolled or resumed");
                    stage="device_presence";var scopedPresence=server.ReportPresence(peer,directCandidates:false);
                    Check(scopedPresence.AccountId==server.AccountId&&scopedPresence.RegisteredPublicIdentity==peer,"Device-bound presence rejected");
                    stage="event_channel_start";server.StartEventChannel(peer,n=>events.Enqueue(n));
                    stage="group_roster_refresh";RefreshGroups();
                    if(command=="status"){Until(()=>server.EventChannelConnected,"event channel");var groups=server.GroupState(peer);Console.WriteLine("authenticated account="+user+" device="+peer+" groups="+groups.Count);foreach(var r in groups)Console.WriteLine("group="+r.GroupId+" legacy="+r.LegacyGroupId+" members="+r.Members.Count);}
                    else if(command=="create-group"){Check(args.Length==7||args.Length==8,"create-group needs name [invite-username]");CreateGroup(args[6],args.Length==8?args[7]:null);}
                    else if(command=="room"){Check(args.Length==10||args.Length==11,"room needs group-id name join|observe hold-seconds [media]");Check(Hex(args[6],32),"Invalid group ID");int hold=Int32.Parse(args[9]);Check(hold>=0&&hold<=600,"Hold seconds must be 0 to 600");Check(args[8]=="join"||args[8]=="observe","Invalid participation mode");Check(args.Length==10||mediaEnabled&&args[8]=="join"&&hold>0,"Media requires joined room and positive hold time");Room(args[6],args[7],args[8]=="join",hold);}
                    else if(command=="dm"){Dm(args[7],Int32.Parse(args[8]));}
                    else {Check(args.Length==8&&Hex(args[6],32),"send needs group-id message");var r=Roster(args[6]);Until(()=>server.EventChannelConnected,"event channel");Check(core.SendGroup(r.LegacyGroupId,args[7]),"Native group send refused: key/control may be unavailable");Console.WriteLine("native_group_message_submitted group="+r.GroupId);DateTime end=DateTime.UtcNow.AddSeconds(20);while(DateTime.UtcNow<end){Pump();Thread.Sleep(50);}Check(outboundQueued&&nativeErrors==0,"Native encrypted group send was not queued cleanly");}
                }finally{StopMailbox();StopRelays();if(server!=null){server.StopEventChannel();server.CloseRpc();}try{passwordSession.Logout();}catch{}}
            }
            return 0;
        }catch(Exception error){Console.Error.WriteLine("probe_failed stage="+stage+" type="+error.GetType().Name+" reason="+SafeFailure(error));return 1;}
    }
}
