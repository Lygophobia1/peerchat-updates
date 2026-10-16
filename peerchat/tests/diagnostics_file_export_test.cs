using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using PeerChatDesktop;

static class DiagnosticsFileExportTest {
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    static void Rejected<T>(Action action)where T:Exception{try{action();throw new Exception("Expected "+typeof(T).Name);}catch(T){}}
    static int Main(string[] args){try{
        string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);
        var rows=new StringBuilder();for(int i=0;i<30000;i++)rows.AppendLine("message_id=message-"+i+" transfer_id=transfer-"+i+" stage=durable_ack retry_ms=9300 sender_device=public-peer");
        const string canonicalTargets="delivery_targets message=3d929b5f4f45431db99b42e02ad55a1d selection=unresolved total=1 acknowledged=1 pending=0 unavailable=0 result=partial";
        rows.AppendLine(canonicalTargets);
        rows.AppendLine("token=LAST_TOKEN_SECRET");rows.AppendLine("message_content=LAST_MESSAGE_PLAINTEXT");rows.AppendLine("file_path=C:\\Users\\PrivateUser\\private.bin");
        rows.AppendLine("message=UNLABELED_MESSAGE_SECRET");rows.AppendLine("group_key=EPOCH_KEY_SECRET");rows.AppendLine("endpoint=https://private-user:URI_PASSWORD_SECRET@example.invalid");
        rows.AppendLine("-----BEGIN PRIVATE KEY-----");rows.AppendLine("PRIVATE_KEY_MATERIAL");rows.AppendLine("-----END PRIVATE KEY-----");rows.AppendLine("last_large_section_marker=完整 Δ 🙂");
        string source=rows.ToString();
        var service=new DiagnosticsService(new DiagnosticsSources{Build=()=>"4.0.2-dev.169",Profile=()=>"fixture",Status=()=>"authenticated",Transfers=()=>source,HistorySync=()=>"scope_current=1\nsource_eligible=1",Connection=()=>"last_success_utc=2026-10-16T00:00:00Z\nlast_failure=relay_pair_timeout",ConnectionEvents=()=>"stage=relay_rendezvous duration_ms=15000 retry_scheduled=1",Local=()=>"eligibility=unavailable reason=native_scope_freshness_or_binding_rejected\npassword=PASSWORD_SECRET\npath=/home/PrivateUser/credentials.json"});
        var complete=service.CaptureComplete();source="CHANGED_AFTER_CAPTURE";
        var failure=new FailureSnapshot{Id="retained-failure-id",OperationType="FileTransfer",OperationId="retained-transfer-id",Stage="native_receive",Reason="transport_io_failure",TimestampUtc="2026-10-16T00:00:01Z",Diagnostics=new DiagnosticsSnapshot{Sections=new List<DiagnosticsSection>{new DiagnosticsSection{Name="late retained section",Details="retained_end_marker=present\nprivate_key=RETAINED_KEY_SECRET"}}}};
        string path=Path.Combine(root,"complete.txt");DiagnosticsFileExport.Save(path,complete,new[]{failure},CancellationToken.None);
        byte[] bytes=File.ReadAllBytes(path);Check(bytes.Length>2*1024*1024,"fixture does not exceed clipboard-size report limits");Check(!(bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf),"unexpected UTF-8 BOM");
        string report=new UTF8Encoding(false,true).GetString(bytes);
        foreach(string expected in new[]{"build=4.0.2-dev.169","snapshot_utc=","snapshot_id=","message_id=message-29999","transfer_id=transfer-29999","last_large_section_marker=完整 Δ 🙂","retained_end_marker=present","operation_id=retained-transfer-id","last_failure=relay_pair_timeout","duration_ms=15000","native_scope_freshness_or_binding_rejected","=== End of complete available diagnostic snapshot ==="})Check(report.Contains(expected),"missing complete field "+expected);
        Check(report.Contains(canonicalTargets),"canonical native delivery target message ID/counters were redacted");
        foreach(string forbidden in new[]{"LAST_TOKEN_SECRET","LAST_MESSAGE_PLAINTEXT","UNLABELED_MESSAGE_SECRET","EPOCH_KEY_SECRET","URI_PASSWORD_SECRET","PrivateUser","PRIVATE_KEY_MATERIAL","RETAINED_KEY_SECRET","PASSWORD_SECRET","CHANGED_AFTER_CAPTURE","diagnostics truncated"})Check(!report.Contains(forbidden),"private/truncated/mutable value leaked "+forbidden);
        Check(service.Capture().Sections.First(s=>s.Name=="File transfers").Details=="CHANGED_AFTER_CAPTURE","bounded capture changed unexpectedly");
        string original="existing user file";File.WriteAllText(path,original);var cancelled=new CancellationTokenSource();cancelled.Cancel();Rejected<OperationCanceledException>(()=>DiagnosticsFileExport.Save(path,complete,null,cancelled.Token));Check(File.ReadAllText(path)==original,"cancel overwrote destination");
        var oversized=new DiagnosticsSnapshot{Sections=new List<DiagnosticsSection>{new DiagnosticsSection{Name="first",Details="partial-prefix"},new DiagnosticsSection{Name="oversized",Details=new string('x',1024*1024+1)}}};Rejected<InvalidDataException>(()=>DiagnosticsFileExport.Save(path,oversized,null,CancellationToken.None));Check(File.ReadAllText(path)==original,"failed report replaced destination");
        string directory=Path.Combine(root,"existing-directory");Directory.CreateDirectory(directory);Rejected<IOException>(()=>DiagnosticsFileExport.Save(directory,complete,null,CancellationToken.None));Check(Directory.Exists(directory),"failed export damaged directory");
        Check(Directory.GetFiles(root,".PeerChat-diagnostics-*.tmp").Length==0,"cancel/failure left partial report");
        Console.WriteLine("PASS complete >2 MiB UTF-8 export, late-section/message/transfer correlation, snapshot freeze, redaction, cancellation, failed write and atomic destination preservation; bytes="+bytes.Length);return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
