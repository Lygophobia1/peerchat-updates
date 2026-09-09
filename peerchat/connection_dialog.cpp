#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include "connection_dialog.hpp"
#include "network.hpp"
#include <cwctype>
#include <stdexcept>

namespace {
enum : int { Host=501, Guest, Name, Address, Port, Key, Generate, Copy };
class ConnectionDialog {
    HWND window{}, owner;
    HFONT font{};
    HBRUSH background = CreateSolidBrush(RGB(22,24,31));
    HBRUSH field = CreateSolidBrush(RGB(40,44,56));
    ConnectionOptions& options;
    bool accepted=false;
    static std::wstring value(HWND window, int id) {
        HWND h=GetDlgItem(window,id);
        std::wstring result(static_cast<size_t>(GetWindowTextLengthW(h))+1,L'\0');
        GetWindowTextW(h,result.data(),static_cast<int>(result.size()));
        result.pop_back();return result;
    }
    HWND control(const wchar_t* type,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id=0) {
        HWND child=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        return child;
    }
    void label(const wchar_t* text,int x,int y,int w,int h=24) {control(L"STATIC",text,0,x,y,w,h);}
    void mode(bool hosting) {
        options.hosting=hosting;
        CheckRadioButton(window,Host,Guest,hosting?Host:Guest);
        SetDlgItemTextW(window,Address,hosting?L"0.0.0.0":L"127.0.0.1");
        SetDlgItemTextW(window,IDOK,hosting?L"Host room":L"Join room");
        EnableWindow(GetDlgItem(window,Generate),hosting);
    }
    void generate() {
        try {SetDlgItemTextW(window,Key,peerchat::Network::generateKey().c_str());}
        catch(const std::exception&) {MessageBoxW(window,L"Windows could not generate a room key.",L"Key generation failed",MB_OK|MB_ICONERROR);}
    }
    void copy() {
        auto key=value(window,Key);
        if(key.empty() || !OpenClipboard(window))return;
        HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,(key.size()+1)*sizeof(wchar_t));
        if(block) {
            void* memory=GlobalLock(block);
            if(memory) {
                memcpy(memory,key.c_str(),(key.size()+1)*sizeof(wchar_t));GlobalUnlock(block);
                EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,block))GlobalFree(block);
            } else GlobalFree(block);
        }
        CloseClipboard();
    }
    void accept() {
        std::wstring name=value(window,Name), port=value(window,Port), address=value(window,Address), key=value(window,Key);
        auto trim=[](std::wstring& s) {
            auto first=s.find_first_not_of(L" \r\n\t"),last=s.find_last_not_of(L" \r\n\t");
            s=first==std::wstring::npos?L"":s.substr(first,last-first+1);
        };
        trim(name);trim(address);trim(key);
        bool validPort=!port.empty() && port.size()<=5;
        for(auto c:port)if(c<L'0'||c>L'9')validPort=false;
        unsigned long p=validPort?wcstoul(port.c_str(),nullptr,10):0;
        if(name.empty() || name.size()>20 || address.empty() || p<1 || p>65535 || !peerchat::Network::validKey(key)) {
            MessageBoxW(window,L"Enter a name (1-20 characters), a numeric IPv4 address, a port (1-65535), and the host's 64-character room key.",L"Check room details",MB_OK|MB_ICONINFORMATION);return;
        }
        options.name=name;options.address=address;options.port=static_cast<unsigned short>(p);options.key=key;
        accepted=true;DestroyWindow(window);
    }
    LRESULT dispatch(UINT m,WPARAM w,LPARAM l) {
        switch(m) {
        case WM_CREATE: {
            font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            control(L"BUTTON",L"Host a room",BS_AUTORADIOBUTTON|WS_TABSTOP|WS_GROUP,24,22,160,28,Host);
            control(L"BUTTON",L"Join a friend",BS_AUTORADIOBUTTON|WS_TABSTOP,206,22,160,28,Guest);
            label(L"Your name",24,76,260);label(L"Port",450,76,130);
            control(L"EDIT",options.name.c_str(),WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER,24,104,400,30,Name);
            control(L"EDIT",std::to_wstring(options.port).c_str(),WS_TABSTOP|ES_NUMBER|WS_BORDER,450,104,130,30,Port);
            label(L"Address  /  Host: 0.0.0.0, Join: your friend's IP",24,153,556);
            control(L"EDIT",options.address.c_str(),WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER,24,181,556,30,Address);
            label(L"Private room key  /  Everyone uses the same key",24,231,556);
            control(L"EDIT",options.key.c_str(),WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER,24,259,556,30,Key);
            control(L"BUTTON",L"New key",WS_TABSTOP,24,306,110,32,Generate);
            control(L"BUTTON",L"Copy key",WS_TABSTOP,144,306,110,32,Copy);
            label(L"Share this key privately. Anyone with it can join.\nThe host relays the room and must remain connected.",24,354,556,48);
            control(L"BUTTON",L"Cancel",WS_TABSTOP,330,425,110,36,IDCANCEL);
            control(L"BUTTON",L"Host room",WS_TABSTOP|BS_DEFPUSHBUTTON,454,425,126,36,IDOK);
            SendDlgItemMessageW(window,Name,EM_SETLIMITTEXT,20,0);
            SendDlgItemMessageW(window,Port,EM_SETLIMITTEXT,5,0);
            SendDlgItemMessageW(window,Address,EM_SETLIMITTEXT,64,0);
            SendDlgItemMessageW(window,Key,EM_SETLIMITTEXT,64,0);
            auto address=options.address;mode(options.hosting);SetDlgItemTextW(window,Address,address.c_str());
            if(options.hosting && options.key.empty())generate();
            return 0;
        }
        case WM_CTLCOLORDLG:case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:
            SetTextColor(reinterpret_cast<HDC>(w),RGB(232,234,244));SetBkColor(reinterpret_cast<HDC>(w),RGB(22,24,31));return reinterpret_cast<LRESULT>(background);
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(w),RGB(232,234,244));SetBkColor(reinterpret_cast<HDC>(w),RGB(40,44,56));return reinterpret_cast<LRESULT>(field);
        case WM_ERASEBKGND: {RECT r;GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(w),&r,background);return 1;}
        case WM_COMMAND:
            switch(LOWORD(w)) {
            case Host:mode(true);break;case Guest:mode(false);break;case Generate:generate();break;case Copy:copy();break;
            case IDOK:accept();break;case IDCANCEL:DestroyWindow(window);break;
            }return 0;
        case WM_CLOSE:DestroyWindow(window);return 0;
        }
        return DefWindowProcW(window,m,w,l);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT m,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<ConnectionDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(m==WM_NCCREATE) {self=static_cast<ConnectionDialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        return self?self->dispatch(m,w,l):DefWindowProcW(window,m,w,l);
    }
public:
    ConnectionDialog(HWND owner,ConnectionOptions& options):owner(owner),options(options){}
    ~ConnectionDialog(){DeleteObject(font);DeleteObject(background);DeleteObject(field);}
    bool run() {
        WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"PeerChatConnection";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassW(&wc);RECT pos;GetWindowRect(owner,&pos);
        CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"Connect your room",WS_CAPTION|WS_SYSMENU,pos.left+160,pos.top+95,622,510,owner,nullptr,wc.hInstance,this);
        if(!window)return false;
        EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetFocus(GetDlgItem(window,Name));
        MSG msg{};while(IsWindow(window) && GetMessageW(&msg,nullptr,0,0)>0)if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        EnableWindow(owner,TRUE);SetActiveWindow(owner);return accepted;
    }
};
}
bool showConnectionDialog(HWND owner,ConnectionOptions& options) {ConnectionDialog dialog(owner,options);return dialog.run();}
