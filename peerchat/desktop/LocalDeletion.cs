using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace PeerChatDesktop {
public sealed partial class CoreAdapter {
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]
    static extern int ui_delete_local(IntPtr handle,string conversation,string id,int group);
    public bool DeleteLocal(string conversation,string id="",bool group=false){
        if(!LocalDeletionState.Canonical(conversation,group?32:64)||!String.IsNullOrEmpty(id)&&!LocalDeletionState.MessageId(id,group))return false;
        lock(mailboxGate){return handle!=IntPtr.Zero&&ui_delete_local(handle,conversation,id??"",group?1:0)!=0;}
    }
}
// Session presentation guard for events already queued before native deletion.
// Native profile storage owns persistent tombstones and replay filtering.
public sealed class LocalDeletionState {
    readonly HashSet<string> hidden=new HashSet<string>(StringComparer.Ordinal);
    readonly Dictionary<string,int> pending=new Dictionary<string,int>(StringComparer.Ordinal);
    public long Revision {get;private set;} public int ExpectedRefreshes {get;private set;}
    public bool ClearingHistory {get;private set;}
    public static bool Canonical(string value,int length){return value!=null&&value.Length==length&&value.All(ch=>ch>='0'&&ch<='9'||ch>='a'&&ch<='f');}
    public static bool MessageId(string id,bool group=false){return Canonical(id,32)||group&&Canonical(id,64);}
    public static string[] MessageIds(MessageModel message,bool group=false){return new[]{message.Data==null?null:message.Data.id,message.Attachment==null?null:message.Attachment.id,message.Attachment==null?null:message.Attachment.logicalId,message.GroupAttachment==null?null:message.GroupAttachment.id,message.GroupAttachment==null?null:message.GroupAttachment.logicalId,message.PendingAttachment==null?null:message.PendingAttachment.AcceptedId}.Where(id=>MessageId(id,group||message.IsGroupMessage||message.GroupAttachment!=null)).Distinct(StringComparer.Ordinal).ToArray();}
    public static string[] Peers(ConversationModel conversation){return conversation.KnownPeerIds.Concat(new[]{conversation.Contact==null?null:conversation.Contact.peer}).Where(peer=>Canonical(peer,64)).Distinct(StringComparer.Ordinal).ToArray();}
    public void Hide(IEnumerable<string> ids){foreach(var id in ids)if(MessageId(id,true))hidden.Add(id);Revision++;}
    public void BeginPending(IEnumerable<string> ids){foreach(var id in ids)if(MessageId(id,true)){int count;pending.TryGetValue(id,out count);pending[id]=count+1;}Revision++;}
    public void EndPending(IEnumerable<string> ids){foreach(var id in ids){int count;if(pending.TryGetValue(id,out count)){if(count<=1)pending.Remove(id);else pending[id]=count-1;}}Revision++;}
    public bool Hidden(params string[] ids){if(ClearingHistory)return true;bool found=ids.Any(id=>id!=null&&hidden.Contains(id));if(found)foreach(var id in ids)if(MessageId(id,true))hidden.Add(id);return found||ids.Any(id=>id!=null&&pending.ContainsKey(id));}
    public void ExpectRefresh(bool wholeHistory){ExpectedRefreshes++;if(wholeHistory)ClearingHistory=true;}
    public bool AcceptRefresh(){if(ExpectedRefreshes==0)return false;ExpectedRefreshes--;if(ExpectedRefreshes==0)ClearingHistory=false;return true;}
    public void Merge(LocalDeletionState other){foreach(var id in other.hidden)hidden.Add(id);ExpectedRefreshes+=other.ExpectedRefreshes;ClearingHistory|=other.ClearingHistory;Revision=Math.Max(Revision,other.Revision);}
}
// Completion callbacks are correlated to durable worker results, not queue acceptance.
public sealed class LocalDeletionCompletion {
    readonly Dictionary<string,Queue<Action<bool>>> waiting=new Dictionary<string,Queue<Action<bool>>>(StringComparer.Ordinal);
    static string Key(string scope,string id,bool group){return (group?"group|":"dm|")+scope+"|"+(id??"");}
    public void Register(string scope,string id,bool group,Action<bool> completion){var key=Key(scope,id,group);Queue<Action<bool>> callbacks;if(!waiting.TryGetValue(key,out callbacks)){callbacks=new Queue<Action<bool>>();waiting.Add(key,callbacks);}callbacks.Enqueue(completion);}
    public bool Complete(string scope,string id,bool group,bool success){var key=Key(scope,id,group);Queue<Action<bool>> callbacks;if(!waiting.TryGetValue(key,out callbacks)||callbacks.Count==0)return false;var callback=callbacks.Dequeue();if(callbacks.Count==0)waiting.Remove(key);callback(success);return true;}
}
public sealed class LocalDeleteMessageEventArgs:RoutedEventArgs {
    public readonly MessageModel Message;public Action Commit;
    public LocalDeleteMessageEventArgs(RoutedEvent routed,MessageModel message):base(routed){Message=message;}
}
public partial class DesktopApplication {
    sealed class LocalDeletionTarget {
        public ConversationModel Conversation;public GroupModel Group;public ulong Profile;public object Owner;public long Account,RemoteAccount;public string Key;public string[] Peers;
        public string Name {get{return Group!=null?Group.Name:Conversation.Name;}}
        public LocalDeletionState State {get{return Group!=null?Group.LocalDeletions:Conversation.LocalDeletions;}}
    }
    LocalDeletionTarget CaptureLocalDeletionTarget(ConversationModel conversation,GroupModel group){
        if(conversation==null&&group==null)return null;
        return new LocalDeletionTarget{Conversation=conversation,Group=group,Profile=profileSessionGeneration,Owner=discovery,Account=discovery==null?0:discovery.AccountId,RemoteAccount=conversation==null?0:conversation.AccountId,Key=group==null?conversation.ConversationId:group.Id,Peers=group==null?LocalDeletionState.Peers(conversation):new[]{group.Id}};
    }
    bool LocalDeletionCurrent(LocalDeletionTarget target){return target!=null&&!closing&&target.Profile==profileSessionGeneration&&target.Owner==discovery&&target.Account==(discovery==null?0:discovery.AccountId)&&(target.Group!=null?model.GroupChats.ContainsKey(target.Key)&&model.GroupChats[target.Key]==target.Group:model.Conversations.Values.Contains(target.Conversation)&&target.Conversation.ConversationId==target.Key&&target.Conversation.AccountId==target.RemoteAccount);}
    readonly LocalDeletionCompletion localDeletionCompletions=new LocalDeletionCompletion();
    bool HandleLocalDeletionEvent(CoreEvent value){
        bool group=value.kind==11||value.source=="local_delete_group";
        bool success=(value.kind==6||value.kind==11)&&value.text=="history_cleared";
        bool failure=value.kind==8&&(value.source=="local_delete_dm"||value.source=="local_delete_group");
        if(!success&&!failure)return false;
        return localDeletionCompletions.Complete(String.IsNullOrEmpty(value.peer)?value.expected:value.peer,value.detail,group,success);
    }
    bool QueueLocalDeletion(LocalDeletionTarget target,IEnumerable<string> ids,bool wholeHistory,Action complete,Action fail){
        if(!LocalDeletionCurrent(target))return false;
        var scopes=target.Group!=null?target.Peers:target.Peers.Concat(LocalDeletionState.Peers(target.Conversation)).Distinct(StringComparer.Ordinal).ToArray();
        var canonical=wholeHistory?new[]{""}:ids.Where(id=>LocalDeletionState.MessageId(id,target.Group!=null)).Distinct(StringComparer.Ordinal).ToArray();if(scopes.Length==0||canonical.Length==0)return false;
        var transientIds=canonical.Where(id=>!String.IsNullOrEmpty(id)).ToArray();target.State.BeginPending(transientIds);if(wholeHistory&&target.Conversation!=null){++dmHistoryEpoch;dmHistoryCursor="";dmHistoryMore=true;}
        int remaining=scopes.Length*canonical.Length;bool allSucceeded=true;
        Action<bool> finish=success=>{allSucceeded&=success;if(--remaining!=0)return;target.State.EndPending(transientIds);if(!LocalDeletionCurrent(target))return;if(allSucceeded)complete();else fail();};
        foreach(var scope in scopes)foreach(var id in canonical){
            bool queued=false;try{queued=LocalDeletionCurrent(target)&&core.DeleteLocal(scope,id,target.Group!=null);}catch{queued=false;}
            if(queued){target.State.ExpectRefresh(wholeHistory);localDeletionCompletions.Register(scope,id,target.Group!=null,success=>{target.State.AcceptRefresh();finish(success);});}else finish(false);
        }
        return true;
    }
    void BindLocalDeletion(){
        window.AddHandler(MessageBubble.LocalDeleteEvent,new RoutedEventHandler((s,e)=>{
            var request=(LocalDeleteMessageEventArgs)e;var message=request.Message;var conversation=model.Conversations.Values.FirstOrDefault(value=>value.Messages.Contains(message));var group=model.GroupChats.Values.FirstOrDefault(value=>value.Messages.Contains(message));var target=CaptureLocalDeletionTarget(conversation,group);
            if(LocalDeletionCurrent(target))request.Commit=()=>DeleteLocalMessage(target,message);e.Handled=true;
        }));
    }
    void BindLocalConversationMenu(ListBox list){
        var menu=new ContextMenu();var clear=new MenuItem{Header="Delete conversation history from this device…"};var remove=new MenuItem{Header="Remove Contact"};menu.Items.Add(clear);menu.Items.Add(new Separator());menu.Items.Add(remove);
        LocalDeletionTarget captured=null;menu.Opened+=(s,e)=>{captured=CaptureLocalDeletionTarget(model.Groups?null:model.Selected,model.Groups?model.SelectedGroup:null);clear.IsEnabled=LocalDeletionCurrent(captured);remove.IsEnabled=captured!=null&&captured.Conversation!=null;};
        clear.Click+=(s,e)=>DeleteLocalConversationHistory(captured);remove.Click+=(s,e)=>{if(LocalDeletionCurrent(captured)&&captured.Conversation==model.Selected&&!model.Groups)RemoveSelectedContact();};
        list.ContextMenu=menu;list.PreviewMouseRightButtonDown+=(s,e)=>{var item=ItemsControl.ContainerFromElement(list,e.OriginalSource as DependencyObject) as ListBoxItem;if(item!=null){item.IsSelected=true;item.Focus();}};
    }
    void PruneLocalAttachmentCatalog(LocalDeletionTarget target,string[] ids,bool wholeHistory){
        if(target.Group!=null)return;
        foreach(var pair in chatCatalog.ToArray()){var row=pair.Value;var conversation=model.FindByPeer(String.IsNullOrEmpty(row.conversationPeer)?row.peer:row.conversationPeer);if(conversation!=target.Conversation)continue;if(wholeHistory||ids.Contains(row.id)||ids.Contains(row.logicalId))chatCatalog.Remove(pair.Key);}
    }
    void DeleteLocalMessage(LocalDeletionTarget target,MessageModel message){
        if(!LocalDeletionCurrent(target))return;var messages=target.Group==null?target.Conversation.Messages:target.Group.Messages;if(!messages.Contains(message))return;
        var ids=LocalDeletionState.MessageIds(message,target.Group!=null);var pending=message.PendingAttachment;
        Action remove=()=>{target.State.Hide(ids);PruneLocalAttachmentCatalog(target,ids,false);if(pending!=null)pending.DeletePresentation();messages.Remove(message);if(target.Group==null)target.Conversation.Refresh();else target.Group.Refresh();RefreshList();Refresh();UpdateUnreadIndicator();};
        if(ids.Length==0){if(pending==null){Notify("This message has no removable local identifier.");return;}remove();return;}
        if(!QueueLocalDeletion(target,ids,false,remove,()=>Notify("The message could not be fully removed from this device’s history. It remains visible; please try again.")))Notify("The local deletion could not be queued. Please try again.");
    }
    void DeleteLocalConversationHistory(LocalDeletionTarget target){
        if(!LocalDeletionCurrent(target))return;
        if(!Confirm("Delete history from this device?","Remove the message and attachment entries with "+target.Name+" from this device’s conversation history? Saved files are kept. Other devices and participants keep their copies. Your friend or group stays in the list. New messages can still appear here.","Delete local history"))return;
        if(!LocalDeletionCurrent(target))return;
        var messages=target.Group==null?target.Conversation.Messages:target.Group.Messages;var removed=messages.ToArray();
        Action complete=()=>{
            PruneLocalAttachmentCatalog(target,new string[0],true);target.State.Hide(removed.Where(message=>message.PendingAttachment==null).SelectMany(message=>LocalDeletionState.MessageIds(message,target.Group!=null)));
            foreach(var message in removed){var pending=message.PendingAttachment;if(pending!=null){pending.DeletePresentation();if(LocalDeletionState.MessageId(pending.AcceptedId,target.Group!=null))DeleteAcceptedPendingAttachment(target.Conversation,target.Group,pending,message,pending.AcceptedId);}messages.Remove(message);}
            if(target.Group!=null){target.Group.Unread=0;target.Group.RequestedHistory=false;target.Group.HistoryLoading=false;target.Group.HistoryExhausted=false;groupAttachmentHistory.Remove(target.Group.Id);target.Group.Refresh();}
            else{var conversation=target.Conversation;conversation.Unread=0;conversation.ResetHistoryPaging();conversation.RequestedHistory=false;conversation.HistoryLoading=false;conversation.HistoryExhausted=false;conversation.LoadingHistoryPeer="";conversation.RequestedPeerHistory.Clear();conversation.HistoryExhaustedPeers.Clear();conversation.Refresh();++dmHistoryEpoch;dmHistoryCursor="";dmHistoryMore=true;}
            RefreshList();Refresh();UpdateUnreadIndicator();
        };
        if(!QueueLocalDeletion(target,new string[0],true,complete,()=>Notify("The history could not be fully cleared on this device. Some entries may already have been removed. Please try again.")))Notify("The local history deletion could not be queued. Please try again.");
    }
    bool DeleteAcceptedPendingAttachment(ConversationModel conversation,GroupModel group,PendingOutgoingAttachment pending,MessageModel message,string id){
        if(!pending.LocalDeleted)return false;var target=CaptureLocalDeletionTarget(conversation,group);
        Action failure=()=>{pending.RestorePresentation();var messages=group==null?conversation.Messages:group.Messages;if(!messages.Contains(message))messages.Add(message);if(group==null)conversation.Refresh();else group.Refresh();RefreshList();Refresh();Notify("The attachment could not be removed from this device’s history. It has been restored here; please try again.");};
        if(!QueueLocalDeletion(target,new[]{id},false,()=>target.State.Hide(new[]{id}),failure))failure();return true;
    }

}
}
