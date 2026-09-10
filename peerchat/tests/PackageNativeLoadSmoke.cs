using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

class PackageNativeLoadSmoke {
    const uint LoadLibrarySearchDllLoadDir=0x00000100;
    const uint LoadLibrarySearchSystem32=0x00000800;
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryEx(string fileName,IntPtr file,uint flags);
    [DllImport("kernel32.dll",CharSet=CharSet.Ansi,SetLastError=true)] static extern IntPtr GetProcAddress(IntPtr module,string name);

    static int Main(string[] args){
        if(args.Length!=1){Console.Error.WriteLine("Usage: PackageNativeLoadSmoke <PeerChat.Core.dll>");return 2;}
        string path=Path.GetFullPath(args[0]);
        IntPtr module=LoadLibraryEx(path,IntPtr.Zero,LoadLibrarySearchDllLoadDir|LoadLibrarySearchSystem32);
        if(module==IntPtr.Zero){int code=Marshal.GetLastWin32Error();Console.Error.WriteLine("FAIL: Windows loader error "+code+": "+new Win32Exception(code).Message);return 1;}
        IntPtr createAddress=GetProcAddress(module,"ui_create"),destroyAddress=GetProcAddress(module,"ui_destroy");
        if(createAddress==IntPtr.Zero||destroyAddress==IntPtr.Zero){Console.Error.WriteLine("FAIL: required native exports are missing");return 1;}
        // Keep the module pinned until process exit, matching the desktop host. The smoke test's
        // purpose is to prove that the Windows loader resolves the packaged core without PATH.
        Console.WriteLine("PASS: packaged native core loads with package/system DLL search only.");return 0;
    }
}
