using System;
using System.Collections.Generic;
using PeerChatDesktop;

namespace PeerChatDesktop {
    public sealed class DiscoverySearchResult {
        public long AccountId;
        public string Username,PublicIdentity;
    }
    public sealed class CoreEvent {
        public string expected,peer,candidate,source,route,detail,text;
        public int kind;
        public long attempt,sessionEpoch,sessionRevision;
        public bool connected;
    }
    public sealed class TestContact { public string peer; }
    public sealed class ConversationModel { public TestContact Contact; public string ConversationId; }
}

internal static class AuthenticatedFriendPublicationTest {
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    static FriendConversationBinding Friend(long account,string current,params string[] known){
        return new FriendConversationBinding{AccountId=account,CurrentPeerId=current,KnownPeerIds=new List<string>(known),ActiveFriend=true,Username="display"};
    }
    public static void Main(){
        var first=Friend(4,new string('a',64),new string('b',64),new string('a',64));
        var second=Friend(5,new string('c',64),new string('c',64));
        var rows=new[]{first,second};
        var publication=new AuthenticatedFriendPublication();
        string mapping=AuthenticatedFriendPublication.Mapping(rows);
        Check(publication.Requires(3,10,20,mapping),"Initial authoritative mapping was skipped.");
        // Publication failure never calls Published: an identical retry stays due.
        Check(publication.Requires(3,10,20,mapping),"Failed publication was cached as success.");
        publication.Published(3,10,20,mapping);
        Check(!publication.Requires(3,10,20,mapping),"Unchanged authenticated mapping invalidated fresh native proof.");
        Check(publication.Requires(0,10,20,mapping),"Unauthenticated context reused an authenticated publication.");
        first.Username="renamed";
        first.KnownPeerIds.Reverse();
        Check(!publication.Requires(3,10,20,AuthenticatedFriendPublication.Mapping(new[]{second,first})),"Display or collection order changed authorization generation.");
        Check(publication.Requires(3,11,20,mapping)&&publication.Requires(3,10,21,mapping)&&publication.Requires(6,10,20,mapping),"Session/profile/account switch kept old authorization generation.");
        first.KnownPeerIds.Add(new string('d',64));
        Check(publication.Requires(3,10,20,AuthenticatedFriendPublication.Mapping(rows)),"New sibling mapping retained stale proof.");
        first.KnownPeerIds.Remove(new string('d',64));
        first.CurrentPeerId=new string('e',64);
        Check(publication.Requires(3,10,20,AuthenticatedFriendPublication.Mapping(rows)),"Current peer replacement retained stale proof.");
        first.CurrentPeerId=new string('a',64);
        first.ActiveFriend=false;
        Check(publication.Requires(3,10,20,AuthenticatedFriendPublication.Mapping(rows)),"Friend revocation retained stale proof.");
        first.ActiveFriend=true;
        publication.Reset();
        Check(publication.Requires(3,10,20,AuthenticatedFriendPublication.Mapping(rows)),"Sign-out did not force republish.");
        Check(publication.Requires(0,10,20,AuthenticatedFriendPublication.Mapping(rows)),"Reset authenticated an account-zero refresh.");
        Console.WriteLine("PASS: unchanged authenticated friend refresh preserves generation; changed mapping, session, profile, account, failed publication and sign-out force republish.");
    }
}
