using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Windows;
using System.Windows.Media;

namespace PeerChatDesktop {
public abstract class ObservableModel : INotifyPropertyChanged {
    public event PropertyChangedEventHandler PropertyChanged;
    protected void Changed(string name){if(PropertyChanged!=null)PropertyChanged(this,new PropertyChangedEventArgs(name));}
}
public class ConversationModel : ObservableModel {
    public ContactData Contact;
    public readonly ObservableCollection<MessageModel> Messages=new ObservableCollection<MessageModel>();
    public string Draft="";
    public bool Online,RequestedHistory;
    public bool PublicAttempted;
    // Presentation state mirrors the core's connection lifecycle. It is deliberately
    // separate from account/friend state so a direct P2P chat can work offline.
    public string ConnectionState="Offline";
    public int Unread;
    public string Name {get{return Contact.name;}}
    public string Initial {get{return String.IsNullOrEmpty(Name)?"?":System.Globalization.StringInfo.GetNextTextElement(Name).ToUpperInvariant();}}
    public Brush Presence {get{return new SolidColorBrush(Online?Color.FromRgb(54,216,115):Color.FromRgb(107,121,135));}}
    public string Preview {get{return Messages.Count>0?Messages.Last().Text:(Contact.verified?"Start a conversation":"Awaiting verification");}}
    public string When {get{return Messages.Count>0?Messages.Last().ShortTime:"";}}
    public string UnreadLabel {get{return Unread>0?Unread+" unread":"";}}
    public void Refresh(){foreach(string name in new[]{"Name","Initial","Presence","Preview","When","UnreadLabel","ConnectionState"})Changed(name);}
}
public class MessageModel : ObservableModel {
    public MessageData Data;
    public bool Outgoing;
    double bubbleWidth=400;
    public double BubbleWidth {get{return bubbleWidth;}set{bubbleWidth=value;Changed("BubbleWidth");}}
    public string Text {get{return Data.text;}}
    public HorizontalAlignment Alignment {get{return Outgoing?HorizontalAlignment.Right:HorizontalAlignment.Left;}}
    public Brush Bubble {get{return new SolidColorBrush(Outgoing?Color.FromRgb(27,73,125):Color.FromRgb(31,42,52));}}
    public DateTime LocalTime {get{try{return new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddMilliseconds(Data.time).ToLocalTime();}catch{return DateTime.MinValue;}}}
    public string ShortTime {get{return LocalTime.Date==DateTime.Today?LocalTime.ToString("h:mm tt"):LocalTime.ToString("MMM d");}}
    public string Stamp {get{return LocalTime.ToString("MMM d · h:mm tt")+(Outgoing?(Data.delivery==2?"  ✓✓ Delivered":"  ◷ Queued"):"");}}
    public void Delivered(){Data.delivery=2;Changed("Stamp");}
}
// Dispatcher-confined presentation state. Core events, never the GUI, determine
// online/trust/delivery state. Switching views retains drafts and history.
public class ChatViewModel {
    public readonly Dictionary<string,ConversationModel> Conversations=new Dictionary<string,ConversationModel>();
    public string OwnId="",Name="You",Status="Opening your encrypted profile…";
    public ushort Port=7777;
    public bool Listening,Groups;
    public ConversationModel Selected;
    public ConversationModel Get(string peer){ConversationModel result;if(!Conversations.TryGetValue(peer,out result)){result=new ConversationModel {Contact=new ContactData{peer=peer,name="Friend",host=""}};Conversations.Add(peer,result);}return result;}
    static void OrderMessages(ConversationModel item){
        var ordered=item.Messages.OrderBy(m=>m.Data.logical).ThenBy(m=>m.Data.sender,StringComparer.Ordinal).ThenBy(m=>m.Data.id,StringComparer.Ordinal).ToList();
        for(int index=0;index<ordered.Count;index++){int current=item.Messages.IndexOf(ordered[index]);if(current!=index)item.Messages.Move(current,index);}
    }
    public void Apply(CoreEvent e){
        if(e.kind==0){OwnId=e.peer;Name=e.text;Port=e.port;Listening=true;Status="Ready to connect";}
        if(e.kind==1&&e.contacts!=null)foreach(var contact in e.contacts){var item=Get(contact.peer);item.Contact=contact;item.Refresh();}
        if(e.kind==3){var item=Get(e.peer);item.Online=true;item.ConnectionState="Ready";item.Refresh();Status=e.text+" connected";}
        if(e.kind==4){var item=Get(e.peer);item.Online=false;item.ConnectionState="Retrying";item.Refresh();Status="Peer disconnected. Reconnect is available.";}
        if(e.kind==2){var item=Get(e.peer);if(e.contacts!=null&&e.contacts.Length>0)item.Contact=e.contacts[0];item.ConnectionState="Awaiting peer verification";item.Online=false;item.Refresh();}
        if(e.kind==9)Status=e.text;
        if((e.kind==5||e.kind==6)&&e.messages!=null){var item=Get(e.peer);foreach(var message in e.messages){if(item.Messages.Any(m=>m.Data.id==message.id))continue;var model=new MessageModel{Data=message,Outgoing=message.sender==OwnId};item.Messages.Add(model);if(e.kind==5&&!model.Outgoing&&(Selected!=item||Groups))item.Unread++;}OrderMessages(item);item.Refresh();}
        // NodeEvent::Delivered is 7 and Error is 8 (the enum is the ABI contract).
        if(e.kind==7&&e.messages!=null)foreach(var item in Conversations.Values)foreach(var receipt in e.messages){var match=item.Messages.FirstOrDefault(m=>m.Data.id==receipt.id);if(match!=null)match.Delivered();}
        if(e.kind==8){var item=String.IsNullOrEmpty(e.peer)?null:Get(e.peer);if(item!=null){item.Online=false;item.ConnectionState=ClassifyConnectionError(e.text);item.Refresh();}Status=e.text;}
    }
    static string ClassifyConnectionError(string text){
        string value=(text??"").ToLowerInvariant();
        if(value.Contains("dns")||value.Contains("hostname")||value.Contains("resolve"))return "DNS/hostname failure";
        if(value.Contains("refused"))return "TCP refused";
        if(value.Contains("timeout")||value.Contains("timed out"))return "TCP timeout";
        if(value.Contains("unreachable")||value.Contains("network route"))return "Offline / unreachable";
        if(value.Contains("tls")||value.Contains("secure session"))return "TLS handshake failure";
        if(value.Contains("identity")||value.Contains("trust"))return "Identity mismatch";
        return "Connection failed";
    }
}
}
