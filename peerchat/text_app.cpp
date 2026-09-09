#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlobj.h>
#include "node.hpp"
#include "share_id.hpp"
#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace ui {
constexpr COLORREF background=RGB(9,13,16),sidebar=RGB(12,17,20),panel=RGB(25,31,36);
constexpr COLORREF border=RGB(48,58,65),foreground=RGB(235,239,242),secondary=RGB(158,169,178);
constexpr COLORREF accent=RGB(90,170,220),highlight=RGB(38,48,56),success=RGB(55,224,111);
class Font {
    HFONT handle;
public:
    explicit Font(int size,int weight=FW_NORMAL):handle(CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI")){}
    ~Font(){DeleteObject(handle);}Font(const Font&)=delete;Font& operator=(const Font&)=delete;
    operator HFONT()const{return handle;}
};
std::wstring widenId(const std::string& value){return {value.begin(),value.end()};}
std::wstring readText(HWND control){std::wstring value(static_cast<size_t>(GetWindowTextLengthW(control))+1,L'\0');GetWindowTextW(control,value.data(),static_cast<int>(value.size()));value.pop_back();return value;}
void fill(HDC drawing,int x,int y,int width,int height,COLORREF color,int radius=0){
    HBRUSH brush=CreateSolidBrush(color);HGDIOBJ oldBrush=SelectObject(drawing,brush),oldPen=SelectObject(drawing,GetStockObject(NULL_PEN));
    if(radius)RoundRect(drawing,x,y,x+width,y+height,radius,radius);else Rectangle(drawing,x,y,x+width,y+height);
    SelectObject(drawing,oldPen);SelectObject(drawing,oldBrush);DeleteObject(brush);
}
void label(HDC drawing,const std::wstring& value,RECT bounds,HFONT font,COLORREF color=foreground,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){
    SelectObject(drawing,font);SetBkMode(drawing,TRANSPARENT);SetTextColor(drawing,color);DrawTextW(drawing,value.c_str(),static_cast<int>(value.size()),&bounds,flags|DT_NOPREFIX);
}
void copy(HWND owner,const std::wstring& text){
    if(!OpenClipboard(owner))return;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
    if(memory){void* buffer=GlobalLock(memory);if(buffer){memcpy(buffer,text.c_str(),(text.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);}else GlobalFree(memory);}
    CloseClipboard();
}
std::wstring timestamp(std::uint64_t milliseconds){
    if(milliseconds>253402300799000ULL)return L"Unknown time";
    std::time_t seconds=static_cast<std::time_t>(milliseconds/1000);std::tm local{};
    if(localtime_s(&local,&seconds)!=0)return L"Unknown time";
    wchar_t value[40]{};wcsftime(value,40,L"%Y-%m-%d  %H:%M",&local);return value;
}

// Small native modal used for both peer addresses and the local profile.
// No network work takes place until the user accepts the entered values.
class DetailsDialog {
    HWND window{},owner;Font font{16};bool profileMode,accepted=false;
    std::wstring& nameOrHost;unsigned short& port;
    HWND field{},portField{}; bool shareMode=false;
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM parameter,LPARAM detail){
        auto* dialog=reinterpret_cast<DetailsDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){dialog=static_cast<DetailsDialog*>(reinterpret_cast<CREATESTRUCTW*>(detail)->lpCreateParams);dialog->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dialog));}
        if(!dialog)return DefWindowProcW(window,message,parameter,detail);
        if(message==WM_CREATE){
            dialog->control(L"STATIC",dialog->shareMode?L"Paste your friend's PeerChat ID":dialog->profileMode?L"Display name":L"Friend's IP address or hostname",0,24,22,440,25,0);
            dialog->field=dialog->control(L"EDIT",dialog->nameOrHost.c_str(),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,24,53,440,30,101);
            if(!dialog->shareMode) dialog->control(L"STATIC",dialog->profileMode?L"Your listening port":L"Friend's listening port",0,24,105,440,25,0);
            if(!dialog->shareMode) dialog->portField=dialog->control(L"EDIT",std::to_wstring(dialog->port).c_str(),WS_BORDER|WS_TABSTOP|ES_NUMBER,24,137,150,30,102);
            dialog->control(L"STATIC",dialog->profileMode?L"Saving restarts your listener and reconnects saved contacts.":L"Compare peer IDs with your friend when verification appears.",0,24,194,440,45,0);
            dialog->control(L"BUTTON",L"Cancel",WS_TABSTOP,224,255,110,36,IDCANCEL);
            dialog->control(L"BUTTON",dialog->shareMode?L"Add friend":dialog->profileMode?L"Save":L"Connect",WS_TABSTOP|BS_DEFPUSHBUTTON,348,255,116,36,IDOK);
            SendMessageW(dialog->field,EM_SETLIMITTEXT,dialog->shareMode?1400:dialog->profileMode?64:253,0);if(dialog->portField)SendMessageW(dialog->portField,EM_SETLIMITTEXT,5,0);return 0;
        }
        if(message==WM_COMMAND){
            if(LOWORD(parameter)==IDOK){auto value=readText(dialog->field);if(dialog->shareMode){dialog->nameOrHost=value;dialog->accepted=true;DestroyWindow(window);return 0;}auto portText=readText(dialog->portField);unsigned long number=wcstoul(portText.c_str(),nullptr,10);
                auto first=value.find_first_not_of(L" \t\r\n"),last=value.find_last_not_of(L" \t\r\n");if(first!=std::wstring::npos)value=value.substr(first,last-first+1);else value.clear();
                if(value.empty()||number==0||number>65535){MessageBoxW(window,L"Enter a name/address and a port from 1 to 65535.",L"Check details",MB_OK);return 0;}
                dialog->nameOrHost=value;dialog->port=static_cast<unsigned short>(number);dialog->accepted=true;DestroyWindow(window);return 0;
            }if(LOWORD(parameter)==IDCANCEL){DestroyWindow(window);return 0;}
        }
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        return DefWindowProcW(window,message,parameter,detail);
    }
    HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id){auto control=CreateWindowW(type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(static_cast<HFONT>(font)),TRUE);return control;}
public:
    DetailsDialog(HWND owner,bool profileMode,std::wstring& value,unsigned short& port,bool share=false):owner(owner),profileMode(profileMode),nameOrHost(value),port(port),shareMode(share){}
    bool show(){
        WNDCLASSW type{};type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=procedure;type.lpszClassName=L"PeerChatDetails";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&type);
        RECT ownerBounds{};GetWindowRect(owner,&ownerBounds);CreateWindowExW(WS_EX_DLGMODALFRAME,type.lpszClassName,shareMode?L"Add friend":profileMode?L"Your profile":L"Connect a friend",WS_CAPTION|WS_SYSMENU,ownerBounds.left+190,ownerBounds.top+130,506,345,owner,nullptr,type.hInstance,this);
        if(!window)return false;
        EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetFocus(field);MSG event{};
        while(IsWindow(window)&&GetMessageW(&event,nullptr,0,0)>0)if(!IsDialogMessageW(window,&event)){TranslateMessage(&event);DispatchMessageW(&event);}
        EnableWindow(owner,TRUE);SetActiveWindow(owner);return accepted;
    }
};

class Application {
    enum Control:UINT {Send=101,AddFriend,AddPeer,ProfileSettings,CopyIdentity,Reconnect,Older,About,DirectMessages,Groups,Settings,ContactList};
    HWND window{},composer{},historyView{},contactList{},searchBox{};
    std::map<UINT,HWND> buttons;
    Font body{16},small{13},heading{25,FW_SEMIBOLD},bold{16,FW_SEMIBOLD};
    HBRUSH panelBrush=CreateSolidBrush(panel);
    HMODULE richEditModule{};
    peerchat::Node peerManager;
    std::wstring dataDirectory,displayName=L"You",status=L"Opening your encrypted profile...";
    std::string ownId,selectedPeer;
    std::vector<peerchat::Contact> contacts;
    std::vector<peerchat::StoredMessage> conversation;
    std::set<std::string> onlinePeers;
    std::map<std::string,int> unread;
    std::vector<peerchat::NodeEvent> verificationQueue;
    bool listening=false,dialogOpen=false,loadingHistory=false,showGroups=false;
    unsigned short listeningPort=7777;
    int width=1180,height=740;
    HWND makeButton(const wchar_t* title,UINT id){HWND button=CreateWindowW(L"BUTTON",title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,window,reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),nullptr,nullptr);buttons[id]=button;return button;}
    const peerchat::Contact* selectedContact()const{for(const auto& contact:contacts)if(contact.peerId==selectedPeer)return &contact;return nullptr;}
    std::wstring contactName(const std::string& peerId)const{if(peerId==ownId)return displayName;for(const auto& contact:contacts)if(contact.peerId==peerId)return contact.name;return L"Friend";}
    void layout(){
        int right=width-232;
        MoveWindow(buttons[AddFriend],16,118,306,38,TRUE);MoveWindow(buttons[CopyIdentity],16,162,306,38,TRUE);MoveWindow(buttons[ProfileSettings],16,206,306,38,TRUE);
        MoveWindow(buttons[DirectMessages],16,274,153,35,TRUE);MoveWindow(buttons[Groups],169,274,153,35,TRUE);MoveWindow(searchBox,16,321,306,34,TRUE);MoveWindow(contactList,16,367,306,std::max(70,height-475),TRUE);
        MoveWindow(buttons[AddPeer],16,height-104,145,30,TRUE);MoveWindow(buttons[Settings],177,height-104,145,30,TRUE);
        MoveWindow(buttons[Reconnect],right-300,24,126,36,TRUE);MoveWindow(buttons[Older],right-166,24,126,36,TRUE);MoveWindow(buttons[About],right+20,height-74,192,36,TRUE);
        MoveWindow(historyView,364,157,right-392,height-277,TRUE);
        MoveWindow(composer,373,height-81,right-506,38,TRUE);MoveWindow(buttons[Send],right-109,height-83,83,40,TRUE);
        InvalidateRect(window,nullptr,FALSE);
    }
    void updateControls(){
        EnableWindow(buttons[Send],!selectedPeer.empty()&&listening);EnableWindow(composer,!selectedPeer.empty()&&listening);
        EnableWindow(buttons[AddFriend],listening);EnableWindow(buttons[AddPeer],listening);EnableWindow(buttons[CopyIdentity],!ownId.empty());EnableWindow(buttons[Reconnect],selectedContact()!=nullptr&&listening);EnableWindow(buttons[Older],!selectedPeer.empty()&&!loadingHistory);EnableWindow(buttons[Groups],TRUE);
        InvalidateRect(window,nullptr,FALSE);
    }
    void updateContacts(){
        SendMessageW(contactList,LB_RESETCONTENT,0,0);int selectedIndex=-1;
        if(showGroups){SendMessageW(contactList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No groups yet"));return;}
        for(size_t index=0;index<contacts.size();index++){
            const auto& contact=contacts[index];auto label=(onlinePeers.count(contact.peerId)?L"\u25cf  ":L"\u25cb  ")+contact.name;
            if(unread[contact.peerId])label+=L" ("+std::to_wstring(unread[contact.peerId])+L")";
            SendMessageW(contactList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(contact.peerId==selectedPeer)selectedIndex=static_cast<int>(index);
        }
        if(selectedIndex>=0)SendMessageW(contactList,LB_SETCURSEL,selectedIndex,0);
    }
    void renderHistory(){
        POINT scrollPosition{};SendMessageW(historyView,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scrollPosition));
        SendMessageW(historyView,WM_SETREDRAW,FALSE,0);SetWindowTextW(historyView,L"");
        auto append=[&](const std::wstring& text,COLORREF color,bool emphasized){
            CHARRANGE selection{-1,-1};SendMessageW(historyView,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
            CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR|CFM_BOLD|CFM_BACKCOLOR;format.crTextColor=color;format.crBackColor=background;format.dwEffects=emphasized?CFE_BOLD:0;
            SendMessageW(historyView,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(historyView,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(text.c_str()));
        };
        if(selectedPeer.empty())append(L"Make yourself at home.\r\n\r\nConnect a friend to start a private conversation.\r\nEach app listens and connects directly.\r\n\r\nYour profile, contacts, and chat history are stored encrypted on this PC.",secondary,false);
        else if(conversation.empty())append(L"The start of your conversation.\r\n\r\nMessages sent while your friend is offline are kept in your encrypted outbox and delivered after reconnecting.",secondary,false);
        for(const auto& message:conversation){
            std::wstring delivery=message.sender==ownId?(message.delivery==peerchat::Delivery::Delivered?L"  /  Delivered":L"  /  Queued"):L"";
            bool outgoing=message.sender==ownId;std::wstring bubble=(outgoing?L"                         ":L"  ")+message.text+L"\r\n";
            append((outgoing?L"                         ":L"  ")+contactName(message.sender)+L"   "+timestamp(message.utcMilliseconds)+delivery+L"\r\n",outgoing?accent:success,true);
            append(bubble+L"\r\n",foreground,false);
        }
        SendMessageW(historyView,WM_SETREDRAW,TRUE,0);SendMessageW(historyView,EM_SCROLL,SB_BOTTOM,0);InvalidateRect(historyView,nullptr,TRUE);
    }
    void selectContact(){
        auto index=SendMessageW(contactList,LB_GETCURSEL,0,0);if(index<0||static_cast<size_t>(index)>=contacts.size())return;
        selectedPeer=contacts[static_cast<size_t>(index)].peerId;unread[selectedPeer]=0;conversation.clear();loadingHistory=true;peerManager.loadHistory(selectedPeer);updateContacts();updateControls();renderHistory();
    }
    void send(){
        auto text=readText(composer);if(text.find_first_not_of(L" \r\n\t")==std::wstring::npos||selectedPeer.empty())return;
        if(peerManager.send(selectedPeer,text).empty())status=L"Message could not be queued. Try again.";else SetWindowTextW(composer,L"");
        updateControls();SetFocus(composer);
    }
    void connectFriend(){
        std::wstring host=L"";unsigned short port=7777;DetailsDialog dialog(window,false,host,port);dialogOpen=true;bool accepted=dialog.show();dialogOpen=false;
        if(accepted){peerManager.connect(host,port);status=L"Connecting to "+host+L":"+std::to_wstring(port)+L"...";updateControls();}
    }
    void addFriend(){
        std::wstring value;unsigned short ignored=0;DetailsDialog dialog(window,false,value,ignored,true);dialogOpen=true;bool accepted=dialog.show();dialogOpen=false;if(!accepted)return;
        peerchat::SharedContact shared;std::wstring error;if(!peerchat::parseShareId(value,shared,error)){MessageBoxW(window,error.c_str(),L"Invalid PeerChat ID",MB_OK|MB_ICONWARNING);return;}
        peerchat::Contact contact{shared.peerId,L"Friend",{},{{shared.host,shared.port}},false};peerManager.saveContact(contact);selectedPeer=contact.peerId;status=L"Friend saved. Connecting to "+shared.host+L":"+std::to_wstring(shared.port)+L"...";updateContacts();updateControls();
    }
    void profileSettings(){
        auto name=displayName;auto port=listeningPort;DetailsDialog dialog(window,true,name,port);dialogOpen=true;bool accepted=dialog.show();dialogOpen=false;
        if(accepted){listening=false;onlinePeers.clear();status=L"Restarting your listener...";peerManager.start(dataDirectory,port,name);updateContacts();updateControls();}
    }
    void poll(){
        bool redrawMessages=false,redrawContacts=false;
        for(auto& event:peerManager.poll()){
            using Kind=peerchat::NodeEvent::Kind;
            switch(event.kind){
            case Kind::Ready:ownId=event.peerId;displayName=event.text;listeningPort=event.port;listening=true;status=L"Listening on port "+std::to_wstring(listeningPort)+L". Ready to connect.";break;
            case Kind::Contacts:contacts=std::move(event.contacts);redrawContacts=true;break;
            case Kind::TrustRequired:verificationQueue.push_back(std::move(event));break;
            case Kind::Connected:onlinePeers.insert(event.peerId);status=event.text+L" connected using TLS.";redrawContacts=true;break;
            case Kind::Disconnected:onlinePeers.erase(event.peerId);status=event.text;redrawContacts=true;break;
            case Kind::Error:status=event.text;break;
            case Kind::History:
                if(event.peerId==selectedPeer){
                    for(auto& message:event.messages)if(std::none_of(conversation.begin(),conversation.end(),[&](const auto& existing){return existing.id==message.id;}))conversation.push_back(std::move(message));
                    std::stable_sort(conversation.begin(),conversation.end(),[](const auto& a,const auto& b){return a.utcMilliseconds<b.utcMilliseconds;});loadingHistory=false;redrawMessages=true;
                }break;
            case Kind::Message:
                if(event.peerId==selectedPeer){for(auto& message:event.messages)conversation.push_back(std::move(message));redrawMessages=true;}
                else {unread[event.peerId]+=static_cast<int>(event.messages.size());redrawContacts=true;}
                if(GetForegroundWindow()!=window){FLASHWINFO flash{sizeof(flash),window,FLASHW_TRAY,2,0};FlashWindowEx(&flash);}break;
            case Kind::Delivered:
                if(event.peerId==selectedPeer){for(const auto& receipt:event.messages)for(auto& message:conversation)if(message.id==receipt.id)message.delivery=peerchat::Delivery::Delivered;redrawMessages=true;}break;
            }
        }
        if(redrawContacts)updateContacts();
        if(redrawMessages)renderHistory();
        updateControls();
        // Authentication decisions stay on the UI thread. A socket handshake
        // proves possession; comparing this persistent ID establishes who owns it.
        if(!dialogOpen&&!verificationQueue.empty()){
            auto request=std::move(verificationQueue.front());verificationQueue.erase(verificationQueue.begin());dialogOpen=true;
            std::wstring explanation=L"Peer name: "+request.text+L"\n\nPeer ID:\n"+widenId(request.peerId)+L"\n\nCompare this ID with your friend using another trusted channel.\nSave and trust this peer only if it matches.\n\nTrust this peer?";
            bool accepted=MessageBoxW(window,explanation.c_str(),L"Verify your friend",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES;
            dialogOpen=false;peerManager.approve(request.peerId,accepted);
        }
    }
    void paint(HDC drawing){
        int right=width-232;
        fill(drawing,0,0,width,height,background);fill(drawing,0,0,338,height,sidebar);fill(drawing,right,0,232,height,sidebar);
        fill(drawing,337,0,1,height,border);fill(drawing,right,0,1,height,border);
        fill(drawing,20,26,38,38,accent,12);label(drawing,L"p",{20,24,58,65},heading,sidebar,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        label(drawing,L"peerchat",{70,26,215,62},heading);label(drawing,L"YOUR PEOPLE. YOUR SPACE.",{20,84,218,106},small,secondary);
        label(drawing,L"CONVERSATIONS",{16,326,310,350},small,secondary);
        label(drawing,displayName,{22,height-48,212,height-20},bold);
        const auto* contact=selectedContact();
        label(drawing,contact?contact->name:L"Your conversations",{364,23,right-170,62},heading);
        bool online=onlinePeers.count(selectedPeer)>0;fill(drawing,right-155,28,128,27,highlight,10);
        label(drawing,online?L"TLS CONNECTED":L"OFFLINE",{right-155,28,right-27,55},small,online?success:secondary,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        label(drawing,contact?L"Direct peer-to-peer messages":L"Good conversations start here.",{364,65,right-26,90},small,secondary);
        fill(drawing,338,105,right-338,1,border);label(drawing,L"PRIVATE CHAT",{364,119,right-26,142},small,secondary);
        fill(drawing,358,height-94,right-378,66,panel,16);
        label(drawing,L"Enter to send  /  Shift+Enter for a new line  /  Encrypted history on this PC",{364,height-25,right-26,height-4},small,secondary);
        fill(drawing,right+16,16,200,156,panel,14);label(drawing,L"MY CONNECTION",{right+32,30,width-30,56},bold);
        label(drawing,listening?L"●  Listening on port "+std::to_wstring(listeningPort):L"○  Listener not ready",{right+32,70,width-30,98},small,listening?success:secondary);
        label(drawing,std::to_wstring(onlinePeers.size())+L" peers connected",{right+32,108,width-30,132},small,secondary);
        fill(drawing,right+16,184,200,142,panel,14);label(drawing,L"MY PEER ID",{right+32,198,width-30,222},bold);
        label(drawing,contact?widenId(contact->peerId):widenId(ownId),{right+32,238,width-30,300},small,foreground,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
        fill(drawing,right+16,342,200,174,panel,14);label(drawing,L"STATUS / SECURITY",{right+32,356,width-30,380},bold);
        label(drawing,L"●  "+status,{right+32,394,width-30,440},small,listening?success:secondary,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
        label(drawing,L"TLS encryption active\r\nLocal history encrypted",{right+32,454,width-30,498},small,secondary,DT_LEFT|DT_WORDBREAK);
        fill(drawing,right+16,532,200,102,panel,14);label(drawing,L"QUICK ACTIONS",{right+32,546,width-30,570},bold);
        label(drawing,L"Copy My ID   •   Reconnect",{right+32,584,width-30,610},small,secondary);
    }
    LRESULT dispatch(UINT message,WPARAM parameter,LPARAM detail){
        switch(message){
        case WM_CREATE:{
            makeButton(L"Send",Send);makeButton(L"+  Add Friend",AddFriend);makeButton(L"+  Connect manually",AddPeer);makeButton(L"My Profile",ProfileSettings);makeButton(L"Copy My ID",CopyIdentity);makeButton(L"Reconnect peer",Reconnect);makeButton(L"Load older messages",Older);makeButton(L"About this build",About);makeButton(L"Direct Messages",DirectMessages);makeButton(L"Groups  (coming soon)",Groups);makeButton(L"Settings",Settings);
            searchBox=CreateWindowW(L"EDIT",L"Search conversations...",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,0,0,10,10,window,nullptr,nullptr,nullptr);
            contactList=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED,0,0,10,10,window,reinterpret_cast<HMENU>(ContactList),nullptr,nullptr);
            composer=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL,0,0,10,10,window,nullptr,nullptr,nullptr);
            historyView=CreateWindowExW(0,L"RICHEDIT50W",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY,0,0,10,10,window,nullptr,nullptr,nullptr);
            if(!historyView)return -1;
            for(HWND control:{composer,contactList,historyView,searchBox})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(static_cast<HFONT>(body)),TRUE);
            SendMessageW(composer,EM_SETLIMITTEXT,16384,0);SendMessageW(historyView,EM_SETBKGNDCOLOR,0,background);SendMessageW(historyView,EM_AUTOURLDETECT,TRUE,0);SendMessageW(historyView,EM_SETEVENTMASK,0,ENM_LINK);SendMessageW(historyView,EM_EXLIMITTEXT,0,4*1024*1024);
            SetTimer(window,1,40,nullptr);peerManager.start(dataDirectory);renderHistory();updateControls();return 0;
        }
        case WM_TIMER:poll();return 0;
        case WM_COMMAND:
            if(LOWORD(parameter)==ContactList&&HIWORD(parameter)==LBN_SELCHANGE){selectContact();return 0;}
            switch(LOWORD(parameter)){
            case Send:send();break;case AddFriend:addFriend();break;case AddPeer:connectFriend();break;case ProfileSettings:case Settings:profileSettings();break;case DirectMessages:showGroups=false;selectedPeer.clear();updateContacts();renderHistory();break;case Groups:showGroups=true;selectedPeer.clear();updateContacts();renderHistory();break;case CopyIdentity:{wchar_t host[256]{};DWORD length=256;if(!GetComputerNameExW(ComputerNamePhysicalDnsFullyQualified,host,&length))GetComputerNameW(host,&length);peerchat::SharedContact shared{host[0]?host:L"localhost",listeningPort,ownId};auto value=peerchat::encodeShareId(shared);copy(window,value);status=L"Your shareable PeerChat ID was copied.";break;}
            case Reconnect:if(const auto* contact=selectedContact();contact&&!contact->endpoints.empty()){const auto& endpoint=contact->endpoints.front();peerManager.connect(endpoint.host,endpoint.port,contact->peerId);}break;
            case Older:if(!selectedPeer.empty()){std::int64_t before=INT64_MAX;for(const auto& message:conversation)if(message.sequence>0)before=std::min(before,message.sequence);loadingHistory=true;peerManager.loadHistory(selectedPeer,before);}break;
            case About:dialogOpen=true;MessageBoxW(window,L"PeerChat / Windows text milestone\n\nEvery instance listens and connects directly.\nTLS authenticates persistent public-key identities.\nCompare IDs before trusting a new contact.\n\nHistory and contacts are encrypted with Windows DPAPI.\nThey belong to this Windows account; protect your account\nand back up your PC. No central history service exists.\n\nManual IP/hostname and port connections are supported.\nPort forwarding or a VPN may be needed across the Internet.\nVoice and other media are deferred until text is stable.",L"About PeerChat",MB_OK);dialogOpen=false;break;
            }updateControls();return 0;
        case WM_NOTIFY:{auto* notice=reinterpret_cast<NMHDR*>(detail);if(notice->code==EN_LINK){auto* link=reinterpret_cast<ENLINK*>(detail);if(link->msg==WM_LBUTTONUP){LONG length=link->chrg.cpMax-link->chrg.cpMin;if(length>0&&length<8192){std::wstring url(static_cast<size_t>(length)+1,L'\0');TEXTRANGEW range{link->chrg,url.data()};SendMessageW(historyView,EM_GETTEXTRANGE,0,reinterpret_cast<LPARAM>(&range));url.resize(length);
                    // Only explicit clicks on HTTP(S) links open the browser.
                    // Incoming HTML is always plain text; it is never executed.
                    if(url.rfind(L"https://",0)==0||url.rfind(L"http://",0)==0)ShellExecuteW(window,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                }}}return 0;}
        case WM_SIZE:width=LOWORD(detail);height=HIWORD(detail);layout();return 0;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(detail)->ptMinTrackSize={1000,690};return 0;
        case WM_MEASUREITEM:{auto* measure=reinterpret_cast<MEASUREITEMSTRUCT*>(detail);if(measure->CtlID==ContactList){measure->itemHeight=64;return TRUE;}break;}
        case WM_CTLCOLORLISTBOX:case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(parameter),foreground);SetBkColor(reinterpret_cast<HDC>(parameter),panel);return reinterpret_cast<LRESULT>(panelBrush);
        case WM_DRAWITEM:{auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(detail);if(item->CtlID==ContactList){int index=static_cast<int>(item->itemID);if(index>=0&&static_cast<size_t>(index)<contacts.size()){auto& contact=contacts[static_cast<size_t>(index)];bool selected=(item->itemState&ODS_SELECTED)!=0;fill(item->hDC,4,2,item->rcItem.right-8,item->rcItem.bottom-4,selected?highlight:panel,12);fill(item->hDC,14,16,30,30,selected?accent:border,15);std::wstring initial=contact.name.empty()?L"?":contact.name.substr(0,1);label(item->hDC,initial,{14,16,44,47},bold,selected?sidebar:foreground,DT_CENTER|DT_VCENTER|DT_SINGLELINE);bool online=onlinePeers.count(contact.peerId)>0;label(item->hDC,(online?L"●  ":L"○  ")+contact.name,{56,10,item->rcItem.right-74,34},bold,online?success:foreground);label(item->hDC,unread[contact.peerId]?L"New message":L"Direct message",{56,35,item->rcItem.right-74,57},small,secondary);label(item->hDC,online?L"Online":L"Offline",{item->rcItem.right-70,35,item->rcItem.right-12,57},small,online?success:secondary,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}return TRUE;}bool primary=item->CtlID==Send||item->CtlID==AddPeer;bool enabled=(item->itemState&ODS_DISABLED)==0;bool segment=item->CtlID==DirectMessages||item->CtlID==Groups;COLORREF buttonColor=(segment&&((item->CtlID==Groups)==showGroups))?foreground:(primary&&enabled?accent:panel);fill(item->hDC,0,0,item->rcItem.right,item->rcItem.bottom,buttonColor,12);auto title=readText(item->hwndItem);RECT bounds=item->rcItem;InflateRect(&bounds,-7,0);label(item->hDC,title,bounds,bold,!enabled?secondary:((segment&&((item->CtlID==Groups)==showGroups))?sidebar:(primary?sidebar:foreground)),DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(item->itemState&ODS_FOCUS){InflateRect(&bounds,-2,-4);DrawFocusRect(item->hDC,&bounds);}return TRUE;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT paintState{};HDC drawing=BeginPaint(window,&paintState),buffer=CreateCompatibleDC(drawing);HBITMAP bitmap=CreateCompatibleBitmap(drawing,width,height);auto old=SelectObject(buffer,bitmap);paint(buffer);BitBlt(drawing,0,0,width,height,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paintState);return 0;}
        case WM_DESTROY:KillTimer(window,1);peerManager.stop();PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(window,message,parameter,detail);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM parameter,LPARAM detail){auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(detail)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}return app?app->dispatch(message,parameter,detail):DefWindowProcW(window,message,parameter,detail);}
public:
    explicit Application(std::wstring directory):dataDirectory(std::move(directory)){}
    ~Application(){DeleteObject(panelBrush);if(richEditModule)FreeLibrary(richEditModule);}
    int run(HINSTANCE instance,int show){
        richEditModule=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!richEditModule)return 1;
        WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=instance;type.lpszClassName=L"PeerChatText";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&type);
        if(!CreateWindowW(type.lpszClassName,L"PeerChat",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1200,800,nullptr,nullptr,instance,this))return 1;
        ShowWindow(window,show);UpdateWindow(window);MSG event{};
        while(GetMessageW(&event,nullptr,0,0)>0){if(event.hwnd==composer&&event.message==WM_KEYDOWN&&event.wParam==VK_RETURN&&!(GetKeyState(VK_SHIFT)&0x8000)){send();continue;}if(!IsDialogMessageW(window,&event)){TranslateMessage(&event);DispatchMessageW(&event);}}
        return static_cast<int>(event.wParam);
    }
};
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int show){
    int argumentCount=0;LPWSTR* arguments=CommandLineToArgvW(GetCommandLineW(),&argumentCount);std::wstring directory;
    for(int index=1;index+1<argumentCount;index++)if(std::wstring(arguments[index])==L"--data-dir")directory=arguments[++index];
    if(arguments)LocalFree(arguments);
    if(directory.empty()){PWSTR localAppData=nullptr;if(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&localAppData)!=S_OK)return 1;directory=(std::filesystem::path(localAppData)/L"PeerChat").wstring();CoTaskMemFree(localAppData);}
    try{ui::Application app(directory);return app.run(instance,show);}catch(...){MessageBoxW(nullptr,L"PeerChat could not start. Check that your profile folder is writable.",L"PeerChat",MB_OK|MB_ICONERROR);return 1;}
}
