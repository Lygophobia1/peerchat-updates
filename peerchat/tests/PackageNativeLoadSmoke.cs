using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

class PackageNativeLoadSmoke {
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate IntPtr Create();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate void Destroy(IntPtr handle);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate IntPtr Snapshot(IntPtr handle);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]delegate int ProviderVersion();
    const uint LoadLibrarySearchDllLoadDir=0x00000100;
    const uint LoadLibrarySearchSystem32=0x00000800;
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryEx(string fileName,IntPtr file,uint flags);
    [DllImport("kernel32.dll",CharSet=CharSet.Ansi,SetLastError=true)] static extern IntPtr GetProcAddress(IntPtr module,string name);

    static int Main(string[] args){
        if(args.Length!=1){Console.Error.WriteLine("Usage: PackageNativeLoadSmoke <PeerChat.Core.dll>");return 2;}
        string path=Path.GetFullPath(args[0]);
        string hpkePath=Path.Combine(Path.GetDirectoryName(path),"PeerChat.Hpke.dll");
        if(File.Exists(hpkePath)){
            IntPtr hpke=LoadLibraryEx(hpkePath,IntPtr.Zero,LoadLibrarySearchDllLoadDir|LoadLibrarySearchSystem32);
            if(hpke==IntPtr.Zero){Console.Error.WriteLine("FAIL: packaged HPKE runtime failed to load");return 1;}
            IntPtr ping=GetProcAddress(hpke,"pc_go_hpke_ping");
            if(ping==IntPtr.Zero||((ProviderVersion)Marshal.GetDelegateForFunctionPointer(ping,typeof(ProviderVersion)))()!=12608)return 1;
            Console.WriteLine("PASS: packaged Go HPKE initializes under restricted search without external Go installation.");
        }
        IntPtr module=LoadLibraryEx(path,IntPtr.Zero,LoadLibrarySearchDllLoadDir|LoadLibrarySearchSystem32);
        if(module==IntPtr.Zero){int code=Marshal.GetLastWin32Error();Console.Error.WriteLine("FAIL: Windows loader error "+code+": "+new Win32Exception(code).Message);return 1;}
        IntPtr createAddress=GetProcAddress(module,"ui_create"),destroyAddress=GetProcAddress(module,"ui_destroy");
        if(createAddress==IntPtr.Zero||destroyAddress==IntPtr.Zero){Console.Error.WriteLine("FAIL: required native exports are missing");return 1;}
        var group=GetProcAddress(module,"pc_group_voice_create");
        if(group!=IntPtr.Zero){
            foreach(string export in new[]{"pc_group_voice_destroy","pc_group_voice_start","pc_group_voice_reconcile","pc_group_voice_signal","pc_group_voice_configure","pc_group_voice_take_signals","pc_group_voice_snapshot","pc_group_voice_mute","pc_group_voice_deafen","pc_group_voice_stop"})if(GetProcAddress(module,export)==IntPtr.Zero){Console.Error.WriteLine("FAIL: group voice ABI missing "+export);return 1;}
            var create=(Create)Marshal.GetDelegateForFunctionPointer(group,typeof(Create));var destroy=(Destroy)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module,"pc_group_voice_destroy"),typeof(Destroy));
            var snapshot=(Snapshot)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module,"pc_group_voice_snapshot"),typeof(Snapshot));
            for(int i=0;i<15;i++){var handle=create();if(handle==IntPtr.Zero)return 1;try{if(!Marshal.PtrToStringUni(snapshot(handle)).Contains("\"active\":false"))return 1;}finally{destroy(handle);}}
            string receive=Path.Combine(Path.GetDirectoryName(path),"PeerChat.AudioReceive.dll");
            IntPtr audio=LoadLibraryEx(receive,IntPtr.Zero,LoadLibrarySearchDllLoadDir|LoadLibrarySearchSystem32);
            if(audio==IntPtr.Zero||GetProcAddress(audio,"pc_rx_create")==IntPtr.Zero){Console.Error.WriteLine("FAIL: packaged AudioReceive component unavailable");return 1;}
            Console.WriteLine("PASS: group voice C ABI exports, 15 create/snapshot/destroy lifetimes and packaged AudioReceive load under restricted search.");
        }
        var screen=GetProcAddress(module,"pc_group_screen_create");
        if(screen!=IntPtr.Zero){
            foreach(string export in new[]{"pc_group_screen_destroy","pc_group_screen_start","pc_group_screen_configure","pc_group_screen_reconcile","pc_group_screen_signal","pc_group_screen_pump","pc_group_screen_take_signals","pc_group_screen_snapshot","pc_group_screen_stop"})if(GetProcAddress(module,export)==IntPtr.Zero){Console.Error.WriteLine("FAIL: group screen ABI missing "+export);return 1;}
            var create=(Create)Marshal.GetDelegateForFunctionPointer(screen,typeof(Create));var destroy=(Destroy)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module,"pc_group_screen_destroy"),typeof(Destroy));
            var snapshot=(Snapshot)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module,"pc_group_screen_snapshot"),typeof(Snapshot));
            for(int i=0;i<15;i++){var handle=create();if(handle==IntPtr.Zero)return 1;try{if(!Marshal.PtrToStringUni(snapshot(handle)).Contains("\"active\":false"))return 1;}finally{destroy(handle);}}
            Console.WriteLine("PASS: group screen C ABI exports and 15 create/snapshot/destroy lifetimes under restricted search.");
        }
        // Keep the module pinned until process exit, matching the desktop host. The smoke test's
        // purpose is to prove that the Windows loader resolves the packaged core without PATH.
        Console.WriteLine("PASS: packaged native core loads with package/system DLL search only.");return 0;
    }
}
