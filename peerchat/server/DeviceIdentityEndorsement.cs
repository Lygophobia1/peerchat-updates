using System;
using System.Linq;

namespace PeerChat.Server {
// Public evidence parser only. A valid signature never registers an authority,
// grants device ownership, or bypasses current directory/revocation admission.
internal sealed class DeviceIdentityPossession {
    internal string Server,Device;
    internal long Account;
    internal byte[] Public,Wire;
    internal static DeviceIdentityPossession Parse(byte[] wire) {
        MailboxProtocol.Require(wire!=null&&wire.Length<=1024);
        var outer=new MailboxProtocol.Reader(wire,"PeerChat/account-device-possession-signed/v1");
        var payload=outer.Field(768);var signature=outer.Field(64);outer.End();
        var r=new MailboxProtocol.Reader(payload,"PeerChat/account-device-possession/v1");
        var p=new DeviceIdentityPossession{Server=r.Text(64),Account=r.Positive(),Device=r.Text(64),Public=r.Field(72),Wire=(byte[])wire.Clone()};r.End();
        MailboxProtocol.Require(MailboxProtocol.Id(p.Server,64)&&MailboxProtocol.Id(p.Device,64)&&
            MailboxProtocol.Verify(payload,signature,p.Public,p.Device));return p;
    }
}
internal sealed class DeviceIdentityEndorsement {
    internal DeviceIdentityPossession Target;
    internal string Authority,Issuance,Session;
    internal byte[] AuthorityKey,Wire;
    internal long AuthorityGeneration,TargetGeneration,Revision,Issued,Expires;
    internal static DeviceIdentityEndorsement Parse(byte[] wire) {
        MailboxProtocol.Require(wire!=null&&wire.Length<=2048);
        var outer=new MailboxProtocol.Reader(wire,"PeerChat/account-device-endorsement-signed/v1");
        var payload=outer.Field(1792);var signature=outer.Field(64);outer.End();
        var r=new MailboxProtocol.Reader(payload,"PeerChat/account-device-endorsement/v1");
        var e=new DeviceIdentityEndorsement{Target=DeviceIdentityPossession.Parse(r.Field(1024)),
            Authority=r.Text(64),AuthorityKey=r.Field(72),AuthorityGeneration=r.Positive(),
            TargetGeneration=r.Positive(),Revision=r.Positive(),Issuance=r.Text(64),Session=r.Text(64),
            Issued=r.Positive(),Expires=r.Positive(),Wire=(byte[])wire.Clone()};r.End();
        MailboxProtocol.Require(MailboxProtocol.Id(e.Authority,64)&&e.Authority!=e.Target.Device&&
            MailboxProtocol.Id(e.Issuance,64)&&MailboxProtocol.Id(e.Session,64)&&e.Expires>e.Issued&&
            e.Expires-e.Issued<=60000&&MailboxProtocol.Verify(payload,signature,e.AuthorityKey,e.Authority));return e;
    }
    // Only for enrollment commit, never durable certificate expiry. Caller must
    // atomically consume the challenge and recheck ownership/revocation/revision.
    internal bool MatchesChallenge(string server,long account,string authority,byte[] authorityKey,
        string target,byte[] targetKey,long authorityGeneration,long targetGeneration,long revision,
        string issuance,string session,long issued,long expires,long now) {
        return now>=Issued&&now<Expires&&Target.Server==server&&Target.Account==account&&Authority==authority&&
            AuthorityKey.SequenceEqual(authorityKey)&&Target.Device==target&&Target.Public.SequenceEqual(targetKey)&&
            AuthorityGeneration==authorityGeneration&&TargetGeneration==targetGeneration&&Revision==revision&&
            Issuance==issuance&&Session==session&&Issued==issued&&Expires==expires;
    }
}
}
