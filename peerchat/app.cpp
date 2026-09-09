#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <ctime>
#include "network.hpp"
#include "audio.hpp"
#include "connection_dialog.hpp"

namespace theme {
constexpr COLORREF bg=RGB(22,24,31), sidebar=RGB(17,19,26), panel=RGB(29,32,42);
constexpr COLORREF border=RGB(45,49,62), text=RGB(232,234,244), muted=RGB(143,151,172);
constexpr COLORREF accent=RGB(153,131,255), selected=RGB(49,43,73), green=RGB(109,219,175);
}
// A tiny RAII wrapper: the font is released when its C++ owner is destroyed.
class Font {
    HFONT value;
public:
    Font(int size, int weight=FW_NORMAL):value(CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI")){}
    ~Font(){DeleteObject(value);}
    Font(const Font&)=delete;
    Font& operator=(const Font&)=delete;
    operator HFONT() const{return value;}
};
struct Message {std::wstring body, time, name;int sender;};
class ChatApp {
    HWND window{}, input{};
    std::array<HWND,9> buttons{};
    Font normal{16}, small{13}, title{25,FW_SEMIBOLD}, bold{16,FW_SEMIBOLD}, logo{22,FW_BOLD};
    HBRUSH inputBrush=CreateSolidBrush(theme::panel);
    std::array<std::vector<Message>,2> messages;
    int channel=0, width=1180, height=740, scroll=0;
    bool joined=false, micMuted=true, connected=false, connecting=false;
    int selfId=0;
    peerchat::Network network;
    peerchat::Audio audio;
    ConnectionOptions options;
    std::vector<peerchat::Peer> peers;
    std::wstring status=L"Host a room or join a friend to get started.";
    std::array<int,2> unread{};
    enum : UINT { Send=101, General, Gaming, Join, Mute, Clear, About, Connect, CopyKey };
    WNDPROC originalEdit{};
    static LRESULT CALLBACK editProc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* app=reinterpret_cast<ChatApp*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_KEYDOWN && w==VK_RETURN){app->send();return 0;}
        if(m==WM_CHAR && w==L'\r')return 0;
        return CallWindowProcW(app->originalEdit,h,m,w,l);
    }
    static void rect(HDC dc,int x,int y,int w,int h,COLORREF color,int radius=0){
        HBRUSH brush=CreateSolidBrush(color); auto old=SelectObject(dc,brush);
        auto pen=SelectObject(dc,GetStockObject(NULL_PEN));
        if(radius)RoundRect(dc,x,y,x+w,y+h,radius,radius);else Rectangle(dc,x,y,x+w,y+h);
        SelectObject(dc,pen);SelectObject(dc,old);DeleteObject(brush);
    }
    void text(HDC dc,const std::wstring& s,int x,int y,int w,int h,HFONT font,COLORREF color=theme::text,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE){
        RECT r{x,y,x+w,y+h};SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
        DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,flags|DT_NOPREFIX);
    }
    HWND button(const wchar_t* label,UINT id){
        HWND h=CreateWindowW(L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            0,0,10,10,window,reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),nullptr,nullptr);
        SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(static_cast<HFONT>(bold)),TRUE);return h;
    }
    void layout(){
        const int right=width-232, center=right-250;
        MoveWindow(buttons[0],right-108,height-79,82,40,TRUE);
        MoveWindow(buttons[1],18,164,196,42,TRUE);MoveWindow(buttons[2],18,212,196,42,TRUE);
        MoveWindow(buttons[3],right-149,117,123,35,TRUE);
        MoveWindow(buttons[4],18,height-131,196,40,TRUE);
        MoveWindow(buttons[5],right+20,height-118,192,36,TRUE);
        MoveWindow(buttons[6],right+20,height-74,192,36,TRUE);
        MoveWindow(buttons[7],18,465,196,40,TRUE);
        MoveWindow(buttons[8],right+20,height-162,192,36,TRUE);
        MoveWindow(input,266,height-73,std::max(70,center-149),28,TRUE);
        InvalidateRect(window,nullptr,FALSE);
    }
    void send(){
        if(!connected)return;
        wchar_t buffer[1025]{};GetWindowTextW(input,buffer,1025);
        std::wstring body=buffer;if(body.find_first_not_of(L" \t\r\n")==std::wstring::npos)return;
        if(network.sendText(channel,body)){SetWindowTextW(input,L"");scroll=0;SetFocus(input);}
        else status=L"Message could not be queued. Check the connection and try again.";
        InvalidateRect(window,nullptr,FALSE);
    }
    std::wstring peerName(int id) const {
        for(const auto& p:peers)if(p.id==id)return p.name;
        return id==selfId?options.name:L"Friend";
    }
    void updateControls() {
        SetWindowTextW(buttons[7],connected?L"Disconnect room":connecting?L"Cancel connection":L"Connect room");
        SetWindowTextW(buttons[3],joined?L"Leave voice":L"Join voice");
        SetWindowTextW(buttons[4],micMuted?L"Unmute microphone":L"Mute microphone");
        EnableWindow(buttons[0],connected);EnableWindow(input,connected);
        EnableWindow(buttons[3],connected);EnableWindow(buttons[4],connected&&joined);
        EnableWindow(buttons[8],connected);
        for(int i=0;i<2;i++) {
            std::wstring label=i==0?L"#   general":L"#   gaming";
            if(unread[i])label+=L"  ("+std::to_wstring(unread[i])+L")";
            SetWindowTextW(buttons[1+i],label.c_str());
        }
        InvalidateRect(window,nullptr,FALSE);
    }
    void leaveVoice() {
        audio.stop();joined=false;micMuted=true;
        if(connected)network.setVoice(false,true);
    }
    void disconnect() {
        leaveVoice();network.stop();network.poll();
        connected=false;connecting=false;peers.clear();status=L"Disconnected. You can start or join another room.";updateControls();
    }
    void connectRoom() {
        if(connected||connecting){disconnect();return;}
        if(!showConnectionDialog(window,options))return;
        status=options.hosting?L"Starting your room...":L"Connecting to your friend...";
        connecting=true;updateControls();
        if(options.hosting)network.host(options.address,options.port,options.key,options.name);
        else network.join(options.address,options.port,options.key,options.name);
    }
    void toggleVoice() {
        if(!connected)return;
        if(joined){leaveVoice();status=L"You left voice. Text chat is still connected.";}
        else {
            std::wstring error;
            if(audio.start(error)) {joined=true;micMuted=true;network.setVoice(true,true);status=L"Voice joined. Your microphone is muted. Use headphones.";}
            else status=L"Voice unavailable: "+error;
        }
        updateControls();
    }
    void toggleMute() {
        if(!connected||!joined)return;
        std::wstring error;
        if(audio.setMuted(!micMuted,error)) {micMuted=!micMuted;network.setVoice(true,micMuted);status=micMuted?L"Microphone muted.":L"Microphone live. Everyone in voice can hear you.";}
        else status=L"Microphone unavailable: "+error;
        updateControls();
    }
    void copyKey() {
        if(!connected || !OpenClipboard(window))return;
        HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,(options.key.size()+1)*sizeof(wchar_t));
        if(block) {
            void* memory=GlobalLock(block);
            if(memory){memcpy(memory,options.key.c_str(),(options.key.size()+1)*sizeof(wchar_t));GlobalUnlock(block);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,block))GlobalFree(block);}
            else GlobalFree(block);
        }
        CloseClipboard();status=L"Room key copied. Share it privately with your friends.";InvalidateRect(window,nullptr,FALSE);
    }
    void tick() {
        bool changed=false;
        for(auto& event:network.poll()) {
            using Kind=peerchat::NetEvent::Kind;
            if(event.kind==Kind::Audio) {if(joined)audio.play(event.sender,event.audio);continue;}
            changed=true;
            switch(event.kind) {
            case Kind::Status:case Kind::Error:status=event.text;break;
            case Kind::Connected:
                connected=true;connecting=false;selfId=event.sender;
                status=options.hosting?L"Room ready on port "+std::to_wstring(options.port)+L". Share your IP and room key.":L"Connected to your friend's encrypted room.";
                break;
            case Kind::Disconnected:
                audio.stop();connected=false;connecting=false;joined=false;micMuted=true;peers.clear();
                status=event.text.empty()?L"The room disconnected. Use Connect room to rejoin.":event.text;break;
            case Kind::Roster:peers=event.peers;break;
            case Kind::Text: {
                if(event.channel<0||event.channel>1)break;
                SYSTEMTIME now{};GetLocalTime(&now);wchar_t stamp[16];swprintf(stamp,16,L"%02u:%02u",now.wHour,now.wMinute);
                auto& history=messages[event.channel];history.push_back({event.text,stamp,peerName(event.sender),event.sender});
                if(history.size()>200)history.erase(history.begin());
                if(event.channel!=channel)unread[event.channel]++;
                break;
            }
            case Kind::Audio:break;
            }
        }
        for(auto& frame:audio.takeCaptured())if(connected&&joined&&!micMuted)network.sendAudio(frame);
        auto error=audio.takeError();
        if(!error.empty()) {status=L"Audio: "+error;leaveVoice();changed=true;}
        if(changed)updateControls();
    }
    void paint(HDC dc){
        using namespace theme;
        int right=width-232, cw=right-232;
        rect(dc,0,0,width,height,bg);rect(dc,0,0,232,height,sidebar);rect(dc,right,0,232,height,sidebar);
        rect(dc,231,0,1,height,border);rect(dc,right,0,1,height,border);
        rect(dc,20,26,38,38,accent,12);text(dc,L"p",20,24,38,40,logo,sidebar,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        text(dc,L"peerchat",70,26,145,30,logo);text(dc,L"YOUR PEOPLE. YOUR SPACE.",20,84,204,20,small,muted);
        text(dc,L"TEXT ROOMS",24,126,190,22,small,muted);
        text(dc,L"VOICE LOUNGE",24,291,190,22,small,muted);
        rect(dc,18,328,196,104,panel,14);text(dc,L"The hangout",32,339,170,25,bold);
        text(dc,joined?(micMuted?L"Listening / mic muted":L"Microphone live"):L"A place for your five",32,370,172,22,small,joined?green:muted);
        text(dc,joined?L"Use headphones for calls":L"Join voice when you're ready",32,398,170,20,small,muted);
        text(dc,L"C++ / Windows native",24,520,188,22,small,muted);
        rect(dc,18,height-77,34,34,selected,12);text(dc,L"Y",18,height-77,34,34,bold,accent,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        text(dc,options.name,63,height-83,148,27,bold,theme::text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);text(dc,connected?(options.hosting?L"Room host":L"Connected"):L"Offline",63,height-56,150,20,small,muted);
        text(dc,channel==0?L"#  general":L"#  gaming",258,23,cw-190,34,title);
        rect(dc,right-152,28,126,26,selected,10);text(dc,connected?L"ENCRYPTED":connecting?L"CONNECTING":L"OFFLINE",right-152,28,126,26,small,connected?green:accent,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        text(dc,L"Good conversations start here.",258,62,cw-40,23,small,muted);
        rect(dc,232,96,cw,1,border);
        text(dc,L"The hangout",258,118,cw-205,28,bold);
        text(dc,L"Five seats. No crowd.",258,147,cw-50,22,small,muted);
        int cardWidth=(cw-68)/5;
        for(int i=0;i<5;i++){
            int x=258+i*(cardWidth+4);
            const auto* p=static_cast<size_t>(i)<peers.size()?&peers[i]:nullptr;
            bool inVoice=p&&p->voice;
            rect(dc,x,184,cardWidth,91,inVoice?selected:panel,14);
            text(dc,p?p->name.substr(0,1):L"+",x,188,cardWidth,31,title,p?accent:muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            text(dc,p?p->name:L"Open seat",x+3,220,cardWidth-6,24,small,p?theme::text:theme::muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            text(dc,p?(inVoice?(p->muted?L"Muted":L"Mic on"):L"Text only"):L"",x,248,cardWidth,20,small,inVoice&&!p->muted?green:muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        rect(dc,258,299,cw-52,1,border);
        auto& history=messages[channel];
        if(history.empty()){
            int y=std::max(323,(height+260)/2-85);
            rect(dc,260,y,47,47,selected,16);text(dc,L"#",260,y,47,47,title,accent,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            text(dc,channel==0?L"Make yourself at home.":L"What's everyone playing?",260,y+66,cw-52,40,title);
            text(dc,connected?L"Your room is ready. Send a message to start the conversation.":L"Host a room or join a friend with the Connect room button.",260,y+112,cw-58,44,normal,muted,DT_LEFT|DT_WORDBREAK);
            text(dc,L"Messages are kept in memory for this session only.",260,y+165,cw-58,42,small,muted,DT_LEFT|DT_WORDBREAK);
        }else{
            // Each card reserves four wrapped lines; scrolling moves through older messages.
            int count=std::max(1,(height-421)/122);
            int end=std::max(0,static_cast<int>(history.size())-scroll);
            int start=std::max(0,end-count), y=317;
            for(int i=start;i<end;i++,y+=122){
                rect(dc,258,y+3,32,32,selected,10);text(dc,history[i].name.substr(0,1),258,y+3,32,32,bold,accent,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                text(dc,history[i].name,302,y,180,26,bold,theme::text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);text(dc,history[i].time,492,y,70,26,small,muted);
                text(dc,history[i].body,302,y+29,cw-100,85,normal,theme::text,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
            }
        }
        rect(dc,252,height-90,cw-40,62,panel,16);
        text(dc,L"Enter to send  /  Scroll to browse  /  Nothing is saved on exit",258,height-25,cw-48,19,small,muted);
        text(dc,L"YOUR ROOM",right+22,28,190,28,bold);
        text(dc,std::to_wstring(peers.size())+L" connected / 5 seats",right+22,66,190,22,small,muted);
        for(size_t i=0;i<peers.size()&&i<5;i++) {
            const auto& p=peers[i];int y=111+static_cast<int>(i)*62;
            rect(dc,right+20,y,192,54,panel,14);
            text(dc,p.name.substr(0,1),right+30,y+9,32,34,bold,accent,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            text(dc,p.name,right+72,y+4,129,24,bold,theme::text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            text(dc,p.id==0?L"Host":L"Friend",right+72,y+29,120,20,small,muted);
        }
        if(peers.empty())text(dc,L"A little closer,\neven from far away.",right+22,139,183,70,bold,theme::text,DT_LEFT|DT_WORDBREAK);
        rect(dc,right+20,height-290,192,113,selected,14);
        text(dc,L"CONNECTION",right+33,height-280,168,24,small,accent);
        text(dc,status,right+33,height-249,165,69,small,theme::text,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
    }
    LRESULT dispatch(UINT m,WPARAM w,LPARAM l){
        switch(m){
        case WM_CREATE:
            buttons={button(L"Send",Send),button(L"#   general",General),button(L"#   gaming",Gaming),button(L"Join voice",Join),button(L"Unmute microphone",Mute),button(L"Clear local messages",Clear),button(L"About this build",About),button(L"Connect room",Connect),button(L"Copy room key",CopyKey)};
            input=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,10,10,window,nullptr,nullptr,nullptr);
            SendMessageW(input,WM_SETFONT,reinterpret_cast<WPARAM>(static_cast<HFONT>(normal)),TRUE);
            SendMessageW(input,EM_SETLIMITTEXT,300,0);
            SendMessageW(input,0x1501,TRUE,reinterpret_cast<LPARAM>(L"Message your room..."));
            SetWindowLongPtrW(input,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));
            originalEdit=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(input,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(editProc)));
            SetTimer(window,1,20,nullptr);updateControls();
            return 0;
        case WM_TIMER:tick();return 0;
        case WM_SIZE:width=LOWORD(l);height=HIWORD(l);layout();return 0;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={1100,760};return 0;
        case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(w),theme::text);SetBkColor(reinterpret_cast<HDC>(w),theme::panel);return reinterpret_cast<LRESULT>(inputBrush);
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps),mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,width,height);auto old=SelectObject(mem,bitmap);paint(mem);BitBlt(dc,0,0,width,height,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);EndPaint(window,&ps);return 0;}
        case WM_DRAWITEM:{auto* d=reinterpret_cast<DRAWITEMSTRUCT*>(l);bool selected=(d->CtlID==General&&channel==0)||(d->CtlID==Gaming&&channel==1);bool primary=d->CtlID==Send||d->CtlID==Join;COLORREF bg=primary?theme::accent:selected?theme::selected:theme::panel;if(d->itemState&ODS_SELECTED)bg=theme::border;rect(d->hDC,0,0,d->rcItem.right,d->rcItem.bottom,bg,12);wchar_t label[100];GetWindowTextW(d->hwndItem,label,100);text(d->hDC,label,10,0,d->rcItem.right-20,d->rcItem.bottom,bold,primary?theme::sidebar:selected?theme::accent:theme::text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(d->itemState&ODS_FOCUS){RECT r=d->rcItem;InflateRect(&r,-4,-4);DrawFocusRect(d->hDC,&r);}return TRUE;}
        case WM_MOUSEWHEEL:scroll=std::clamp(scroll+(GET_WHEEL_DELTA_WPARAM(w)>0?1:-1),0,std::max(0,static_cast<int>(messages[channel].size())-1));InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_COMMAND:
            switch(LOWORD(w)){
            case Send:send();break;
            case General:case Gaming:channel=LOWORD(w)==General?0:1;scroll=0;unread[channel]=0;updateControls();break;
            case Join:toggleVoice();break;
            case Mute:toggleMute();break;
            case Connect:connectRoom();break;
            case CopyKey:copyKey();break;
            case Clear:messages[channel].clear();scroll=0;InvalidateRect(window,nullptr,FALSE);break;
            case About:MessageBoxW(window,L"PeerChat / C++ rooms\n\nOne host + four friends. Two text rooms and group voice.\nWindows default audio devices; microphone starts muted.\nUse headphones: echo cancellation is not included.\n\nConnections use a shared room key and AES-GCM.\nThe host can read/relay room traffic. This experimental\nprotocol is not audited and has no forward secrecy.\n\nJoin using the host's reachable IPv4 address, port, and key.\nNo automatic NAT traversal or central service is included.\nMessages disappear when the app closes.",L"About PeerChat",MB_OK);break;
            }return 0;
        case WM_DESTROY:KillTimer(window,1);audio.stop();network.stop();PostQuitMessage(0);return 0;
        }return DefWindowProcW(window,m,w,l);
    }
    static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
        ChatApp* app=reinterpret_cast<ChatApp*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){app=static_cast<ChatApp*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);app->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
        return app?app->dispatch(m,w,l):DefWindowProcW(h,m,w,l);
    }
public:
    ~ChatApp(){DeleteObject(inputBrush);}
    int run(HINSTANCE instance,int show){
        WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"PeerChatCpp";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
        RegisterClassW(&wc);
        if(!CreateWindowW(wc.lpszClassName,L"PeerChat",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1200,800,nullptr,nullptr,instance,this))return 1;
        ShowWindow(window,show);UpdateWindow(window);MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){
            if(msg.hwnd==input && msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN){send();continue;}
            if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        return static_cast<int>(msg.wParam);
    }
};
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int show){ChatApp app;return app.run(instance,show);}

