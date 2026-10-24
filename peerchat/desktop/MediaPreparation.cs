using System;
using System.Threading.Tasks;

namespace PeerChatDesktop {
// UI-thread owned. Share only an unfinished request for exactly this session.
public sealed class MediaPreparation {
    object owner; ulong profile,session; Task<bool> pending;
    public bool Running { get { return pending!=null&&!pending.IsCompleted; } }
    public Task<bool> Run(object currentOwner,ulong currentProfile,ulong currentSession,Func<Task<bool>> request){
        if(Running&&ReferenceEquals(owner,currentOwner)&&profile==currentProfile&&session==currentSession)return pending;
        owner=currentOwner;profile=currentProfile;session=currentSession;
        pending=request();return pending;
    }
}
}
