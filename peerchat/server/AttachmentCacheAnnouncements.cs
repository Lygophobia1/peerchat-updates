using System;
using System.Collections.Generic;
using System.Linq;

namespace PeerChat.Server {
internal sealed partial class AttachmentCacheRuntime {
 internal sealed class Announcement {internal string Id,Message,Digest;internal byte[] Wire,Descriptor;}
 internal void Publish(string id,byte[] wire){lock(gate){Check();var d=Descriptor(id);Require(d.MatchesAnnouncement(wire),"cache_announcement_binding_invalid");
  Require(Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==1,"cache_upload_incomplete");Require(Scalar("SELECT expires FROM objects WHERE id="+Quote(id))>Now(),"cache_attachment_expired");
  var prior=Binary("SELECT wire FROM announcements WHERE object_id="+Quote(id));if(prior!=null){Require(prior.SequenceEqual(wire),"cache_conflicting_announcement");return;}
  // Completion retains a bounded metadata reservation until publication and
  // per-device announcement ACKs have durably consumed it.
  Space(0);ReservedMutation(id,()=>Execute("INSERT INTO announcements VALUES("+Quote(id)+","+Blob(wire)+","+Quote(MailboxProtocol.Digest(wire))+");"));
 }}
 internal Announcement ReadAnnouncement(string id){lock(gate){Check();var d=Descriptor(id);Require(Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==1,"cache_upload_incomplete");Require(Scalar("SELECT expires FROM objects WHERE id="+Quote(id))>Now(),"cache_attachment_expired");var wire=Binary("SELECT wire FROM announcements WHERE object_id="+Quote(id));Require(wire!=null,"cache_announcement_unavailable");Require(d.MatchesAnnouncement(wire),"cache_announcement_binding_invalid");var digest=Rows("SELECT digest FROM announcements WHERE object_id="+Quote(id),1);Require(digest.Count==1&&digest[0][0]==MailboxProtocol.Digest(wire),"cache_announcement_corrupt");return new Announcement{Id=id,Message=d.Message,Wire=wire,Descriptor=(byte[])d.Wire.Clone(),Digest=MailboxProtocol.Digest(wire)};}}
 internal bool AcknowledgeAnnouncement(string id,string device,string digest){lock(gate){Check();Require(MailboxProtocol.Id(device,64)&&MailboxProtocol.Id(digest,64),"cache_announcement_ack_invalid");var value=ReadAnnouncement(id);Require(value.Digest==digest,"cache_announcement_ack_mismatch");Require(Scalar("SELECT count(*) FROM recipients WHERE object_id="+Quote(id)+" AND device="+Quote(device))==1,"cache_original_recipient_required");
  if(Scalar("SELECT count(*) FROM announcement_acks WHERE object_id="+Quote(id)+" AND device="+Quote(device)+" AND digest="+Quote(digest))==1)return false;
  Space(0);ReservedMutation(id,()=>Execute("INSERT INTO announcement_acks VALUES("+Quote(id)+","+Quote(device)+","+Quote(digest)+");"));return true;
 }}
 // Candidate IDs only. Admission must recheck current session and original /
 // current membership before exposing ANY envelope or acknowledging it.
 internal List<string> PendingAnnouncements(long account,string device,string after){lock(gate){Check();Require(account>0&&MailboxProtocol.Id(device,64)&&(after==""||MailboxProtocol.Id(after,32)),"cache_announcement_cursor_invalid");return Rows("SELECT o.id FROM objects o JOIN recipients r ON r.object_id=o.id JOIN announcements n ON n.object_id=o.id WHERE r.account="+account+" AND r.device="+Quote(device)+" AND o.complete=1 AND o.deleting=0 AND o.expires>"+Now()+" AND o.id>"+Quote(after)+" AND NOT EXISTS(SELECT 1 FROM announcement_acks a WHERE a.object_id=o.id AND a.device=r.device) ORDER BY o.id LIMIT 64",64).Select(row=>row[0]).ToList();}}
}
}
