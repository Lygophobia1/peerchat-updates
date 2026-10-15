using System;
using System.Reflection;
class CacheFrameworkStorageProbe {
 static int Main(string[] args){try{
  if(args.Length>2){AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);}
  var assembly=Assembly.LoadFrom(args[0]);var type=assembly.GetType("PeerChat.ServerShared.ServerConfiguration",true);var config=type.GetMethod("Load").Invoke(null,new object[]{args[1]});
  var options=type.GetField("AttachmentCache").GetValue(config);var runtime=assembly.GetType("PeerChat.Server.AttachmentCacheRuntime",true);
  using(var instance=(IDisposable)Activator.CreateInstance(runtime,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{options,(Func<long>)(()=>DateTimeOffset.UtcNow.ToUnixTimeSeconds())},null)){}
  Console.WriteLine("PASS Framework cache storage ownership/open");return 0;
 }catch(Exception error){while(error is TargetInvocationException&&error.InnerException!=null)error=error.InnerException;Console.Error.WriteLine(error);return 1;}}
}
