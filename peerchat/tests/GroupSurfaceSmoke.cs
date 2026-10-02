using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using PeerChatDesktop;
// Synthetic presentation fixture only: no claim of live audio or membership.
class GroupSurfaceSmoke {
    static BindingFlags Private=BindingFlags.Instance|BindingFlags.NonPublic;
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    static object Field(object value,string name){return value.GetType().GetField(name,Private).GetValue(value);}
    static IEnumerable<T> Descendants<T>(DependencyObject root)where T:DependencyObject {foreach(object item in LogicalTreeHelper.GetChildren(root)){var child=item as DependencyObject;if(child==null)continue;if(child is T)yield return (T)child;foreach(var nested in Descendants<T>(child))yield return nested;}}
    [STAThread]static int Main(string[] args){DesktopApplication desktop=null;try{
        var app=new Application();desktop=new DesktopApplication(args[0],0);
        var window=(Window)Field(desktop,"window");var model=(ChatViewModel)Field(desktop,"model");
        var content=(FrameworkElement)window.Content;content.Measure(new Size(1440,940));content.Arrange(new Rect(0,0,1440,940));content.UpdateLayout();
        Check(((StackPanel)window.FindName("ContextConnection")).Visibility==Visibility.Visible,"DM pane changed");
        model.Groups=true;model.OwnId=new string('d',64);
        var group=new GroupData{id=new string('a',32),name="UI Test Group",members=new[]{new GroupMemberData{account=1,peer=model.OwnId,name="Owner",role=3}}};
        model.Apply(new CoreEvent{kind=9,groups=new[]{group}});model.SelectedGroup=model.GroupChats[group.id];
        desktop.GetType().GetMethod("Refresh",Private).Invoke(desktop,null);
        var pane=(StackPanel)Field(desktop,"groupDetails");
        Check(pane.Visibility==Visibility.Visible&&((StackPanel)window.FindName("ContextConnection")).Visibility==Visibility.Collapsed,"Group details not separate");
        var text=Descendants<TextBlock>(pane).Select(t=>t.Text).ToArray();
        foreach(var title in new[]{"Group Details","Members","Voice Rooms","Screen Share Rooms","Group management"})Check(text.Contains(title),"Missing "+title);
        Check(!text.Any(t=>t.Contains("TURN")||t.Contains("Diagnostics")),"Transport diagnostics leaked into group pane");
        Check(((Button)window.FindName("PeerInfo")).Visibility==Visibility.Collapsed,"DM details duplicated in group header");
        Check(((Button)Field(desktop,"groupCreateVoice")).Visibility==Visibility.Visible&&((Button)Field(desktop,"groupCreateScreen")).Visibility==Visibility.Visible,"Room create actions absent");
        Check(Descendants<TextBlock>(pane).All(t=>t.Foreground!=null&&t.Foreground.ToString()!="#FF000000"),"Black group labels");
        string serverGroup=new string('b',32);
        var roster=new ServerGroupRoster{GroupId=serverGroup,LegacyGroupId=group.id,RosterGeneration=1};
        roster.Members.Add(new ServerGroupMember{AccountId=1,PeerId=model.OwnId,Username="Owner",Role="owner",State="member"});
        ((Dictionary<string,ServerGroupRoster>)Field(desktop,"serverGroupRosters"))[group.id]=roster;
        var discovery=new DiscoveryClient("localhost",7443,"synthetic"){AccountId=1};
        desktop.GetType().GetField("discovery",Private).SetValue(desktop,discovery);
        Func<long,string,Dictionary<string,object>> member=(account,name)=>new Dictionary<string,object>{{"accountId",account},{"publicIdentity",new string(account==1?'d':'e',64)},{"username",name},{"mediaGeneration",new string('f',32)}};
        Func<string,string,Dictionary<string,object>> room=(name,kind)=>new Dictionary<string,object>{{"roomId",Guid.NewGuid().ToString("N")},{"kind",kind},{"name",name},{"creatorAccountId",1},{"creatorDevice",model.OwnId},{"members",new object[]{member(2,"Other Member")}},{"sharer",kind=="screen_share"?(object)member(1,"Owner"):null}};
        var state=GroupMediaState.Parse(new Dictionary<string,object>{{"groupId",serverGroup},{"epoch",new string('c',32)},{"revision",1},{"rosterGeneration",1},{"rooms",new object[]{room("General Voice","voice"),room("Raid Team","voice"),room("Share One","screen_share"),room("Share Two","screen_share")}}});
        ((Dictionary<string,GroupMediaState>)Field(discovery,"roomStates"))[serverGroup]=state;
        desktop.GetType().GetMethod("RefreshGroupDetails",Private).Invoke(desktop,new object[]{true});
        text=Descendants<TextBlock>(pane).Select(t=>t.Text).ToArray();
        foreach(string name in new[]{"General Voice","Raid Team","Share One","Share Two"})Check(text.Contains(name),"Multiple authoritative room missing: "+name);
        Check(Descendants<Button>(pane).Count(b=>Convert.ToString(b.Content)=="Join")==2,"Independent voice join actions missing");
        Check(Descendants<Button>(pane).Count(b=>Convert.ToString(b.Content)=="Stop Sharing")==2,"Authoritative screen sharer actions missing");
        Check(!Descendants<Button>(pane).Any(b=>Convert.ToString(b.Content)=="Watch unavailable"),"Unavailable testing surface retained");
        var emptyScreen=room("Empty share","screen_share");emptyScreen["sharer"]=null;
        var remoteScreen=room("Remote share","screen_share");remoteScreen["sharer"]=member(2,"Other Member");
        state=GroupMediaState.Parse(new Dictionary<string,object>{{"groupId",serverGroup},{"epoch",new string('c',32)},{"revision",2},{"rosterGeneration",1},{"rooms",new object[]{room("General Voice","voice"),room("Raid Team","voice"),emptyScreen,remoteScreen}}});
        ((Dictionary<string,GroupMediaState>)Field(discovery,"roomStates"))[serverGroup]=state;
        desktop.GetType().GetMethod("RefreshGroupDetails",Private).Invoke(desktop,new object[]{true});
        Check(Descendants<Button>(pane).Count(b=>Convert.ToString(b.Content)=="Start Sharing")==1,"Inactive room start-share command missing");
        Check(Descendants<Button>(pane).Count(b=>Convert.ToString(b.Content)=="Watch")==1,"Voluntary remote viewing command missing");
        Check(text.Any(t=>t.Contains("Other Member")),"Participant names missing");
        var firstControl=pane.Children[0];
        var repeatedStatus=typeof(GroupVoiceRoomStatus).GetConstructors(Private)[0].Invoke(new object[]{"","","Not joined","",false,false,false});
        typeof(GroupVoiceService).GetField("status",Private).SetValue(Field(desktop,"groupVoice"),repeatedStatus);
        desktop.GetType().GetMethod("RefreshGroupDetails",Private).Invoke(desktop,new object[]{false});
        Check(Object.ReferenceEquals(firstControl,pane.Children[0]),"Unchanged voice presentation rebuilt controls on every pulse");
        var column=(ColumnDefinition)window.FindName("RightPaneColumn");Check(column.Width.Value>0,"Pane not open by default");
        var toggle=desktop.GetType().GetMethod("ToggleRightPane",Private);toggle.Invoke(desktop,null);Check(column.Width.Value==0&&((Border)window.FindName("RightDetailsPane")).Visibility==Visibility.Collapsed,"Pane width not reclaimed");
        toggle.Invoke(desktop,null);Check(column.Width.Value>0&&pane.Visibility==Visibility.Visible,"Pane not restored");
        model.Groups=false;desktop.GetType().GetMethod("Refresh",Private).Invoke(desktop,null);Check(pane.Visibility==Visibility.Collapsed&&((StackPanel)window.FindName("ContextConnection")).Visibility==Visibility.Visible,"DM pane not restored");
        Console.WriteLine("PASS: synthetic WPF group layout, separate DM/group panes, room actions, dark labels, collapse/reopen and unchanged DM surface");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}finally{if(desktop!=null){((GroupScreenShareService)Field(desktop,"groupScreen")).Dispose();((GroupVoiceService)Field(desktop,"groupVoice")).Dispose();((CoreAdapter)Field(desktop,"core")).Dispose();}}}
}
