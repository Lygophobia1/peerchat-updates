using System;
using System.Linq;
using System.Collections.Generic;
using System.Windows;
namespace PeerChatDesktop {
public partial class DesktopApplication {
    // Explicit developer-only mode; never enabled by opening diagnostics normally.
    bool connectionsDeveloperPreview=Environment.GetEnvironmentVariable("PEERCHAT_DIAGNOSTICS_CONNECTIONS_PREVIEW")=="1";
    ExistingConnectionsProvider existingConnectionsProvider;
    readonly List<Action> connectionViewRefreshes=new List<Action>();string connectionsViewContext="";DateTime nextConnectionsViewUtc;
    ConnectionPreviewSnapshot ReadConnectionsView(){if(connectionsDeveloperPreview)return connectionPreviewProvider.Read(connectionPreviewState);if(existingConnectionsProvider==null)existingConnectionsProvider=new ExistingConnectionsProvider(CaptureExistingConnections);return existingConnectionsProvider.Read();}
    ConnectionsLiveInput CaptureExistingConnections(){
        var routes=model.Conversations.Values.Select(c=>{var d=connectionAttempts.For(c);bool ready=d.HasLiveSession;return new ConnectionsRouteObservation{Selected=c==model.Selected,Target=c.Name,Conversation=c.ConversationId,Peer=c.Contact==null?d.ExpectedPeer:c.Contact.peer,Connected=ready,Epoch=d.SessionEpoch,Revision=d.SessionRevision,WinnerId=d.LiveConnectionId,AttemptId=d.AttemptId,Route=ready?d.LiveRoute:d.Route,Source=ready?d.LiveSource:d.Source,Candidate=ready?d.LiveCandidate:d.Candidate,Direction=d.LiveDirection,Transport=d.TransportState,Trust=d.TrustState,Progress=d.LatestProgress};}).ToArray();
        return new ConnectionsLiveInput{SelectedSessionId=previewSessionId,Profile=localProfile==null?"Local":localProfile.Name,Context="profile-generation:"+profileSessionGeneration+"/authentication-generation:"+authenticatedFriendSnapshotGeneration+"/discovery-generation:"+discoverySessionGeneration+"/local-account:"+(discovery==null?0:discovery.AccountId),AccountContextAvailable=discovery!=null&&discovery.AccountId>0&&!String.IsNullOrEmpty(discovery.SessionToken),DiscoveryConnected=discovery!=null&&discovery.EventChannelConnected,Routes=routes,ConnectionText=ConnectionDiagnosticsText(),RetainedLog=connectionLog.Read(),Failures=diagnosticsRetention==null?new FailureSnapshot[0]:diagnosticsRetention.Failures().ToArray()};
    }
    void BindConnectionsViewRefresh(Window owner){
        EventHandler refresh=(s,e)=>{
            if(!owner.IsVisible||diagnosticContext.Section!=DiagnosticSection.Connections||connectionsDeveloperPreview||DateTime.UtcNow<nextConnectionsViewUtc)return;nextConnectionsViewUtc=DateTime.UtcNow.AddSeconds(1);
            var current=ReadConnectionsView();if(current.Context!=connectionsViewContext){previewSessionId=previewFailureId=previewLogSearch="";previewSeverity=previewType=previewEventSession="All";RenderConnectionPreview(current);return;}
            connectionPreview=current;diagnosticBanner.Text=current.Banner;foreach(var update in connectionViewRefreshes.ToArray())update();
        };
        owner.Loaded+=(s,e)=>timer.Tick+=refresh;
        owner.Closed+=(s,e)=>{timer.Tick-=refresh;connectionViewRefreshes.Clear();existingConnectionsProvider=null;connectionsViewContext="";};
    }
}
}
