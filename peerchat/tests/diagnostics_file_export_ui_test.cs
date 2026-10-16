using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Serialization;
using System.Threading;
using PeerChatDesktop;

static class DiagnosticsFileExportUiTest {
    const BindingFlags Fields=BindingFlags.Instance|BindingFlags.NonPublic;
    static void Set(object owner,string name,object value){owner.GetType().GetField(name,Fields).SetValue(owner,value);}
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    [STAThread]static int Main(){try{
        // Exercise the real UI export capture without native profile creation,
        // filesystem identities, a clipboard, or an interactive save picker.
        var owner=(DesktopApplication)FormatterServices.GetUninitializedObject(typeof(DesktopApplication));
        var model=new ChatViewModel{OwnId="local-public-peer"};
        var first=model.Get("first-peer");first.Messages.Add(new MessageModel{Data=new MessageData{id="first-message-id",sender="first-peer",text="PRIVATE_FIRST_MESSAGE"}});
        var other=model.Get("other-peer");other.Messages.Add(new MessageModel{Data=new MessageData{id="other-message-id",sender="other-peer",text="PRIVATE_OTHER_MESSAGE"}});model.Selected=first;
        Set(owner,"model",model);Set(owner,"diagnosticsService",new DiagnosticsService(new DiagnosticsSources{Build=()=>"fixture",Transfers=()=>new string('x',70000)+"\nlate-source=preserved"}));
        Set(owner,"chatCatalog",new Dictionary<string,CoreAdapter.FileTransferData>{{"file",new CoreAdapter.FileTransferData{id="catalog-transfer-id",peer="recipient-public-peer",senderPeer="sender-public-peer",path="C:\\PrivatePath\\file.bin",savePath="C:\\PrivatePath\\save.bin",name="PRIVATE_FILENAME"}}});
        var snapshot=(DiagnosticsSnapshot)typeof(DesktopApplication).GetMethod("CaptureCompleteDiagnosticSnapshot",Fields).Invoke(owner,null);
        model.Conversations.Clear();
        var text=new StringWriter();DiagnosticsFileExport.Write(text,snapshot,null,CancellationToken.None);string report=text.ToString();
        foreach(string value in new[]{"first-message-id","other-message-id","catalog-transfer-id","sender-public-peer","recipient-public-peer","late-source=preserved"})Check(report.Contains(value),"complete UI capture omitted "+value);
        foreach(string value in new[]{"PRIVATE_FIRST_MESSAGE","PRIVATE_OTHER_MESSAGE","PrivatePath","PRIVATE_FILENAME"})Check(!report.Contains(value),"UI correlation copied content/path "+value);
        Check(typeof(DesktopApplication).GetMethod("ExportCompleteDiagnostics",Fields)!=null,"missing file-export UI action");
        Console.WriteLine("PASS production Windows UI full-capture independent of selected conversation, complete source >65k, frozen metadata-only message/transfer identities, no clipboard or native profile required.");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
