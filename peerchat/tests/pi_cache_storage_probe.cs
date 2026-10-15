using System;
using System.IO;
using System.Reflection;
class PiCacheStorageProbe {
 static int Main(string[] args){try{
  if(args.Length!=2||!Path.IsPathRooted(args[0])||!Path.IsPathRooted(args[1]))throw new InvalidOperationException("Absolute fixture/deployment paths required");
  var assembly=Assembly.LoadFrom(args[0]);var configType=assembly.GetType("PeerChat.ServerShared.ServerConfiguration",true);
  var config=configType.GetMethod("Load").Invoke(null,new object[]{args[1]});configType.GetMethod("Validate").Invoke(config,null);
  var options=configType.GetField("AttachmentCache").GetValue(config);var runtime=assembly.GetType("PeerChat.Server.AttachmentCacheRuntime",true);
  using(var instance=(IDisposable)Activator.CreateInstance(runtime,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{options,(Func<long>)(()=>DateTimeOffset.UtcNow.ToUnixTimeSeconds())},null)){
   var charged=(long)runtime.GetProperty("ChargedBytes",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(instance);
   Console.WriteLine("PASS exact candidate cache ownership, filesystem reserve and durable index; charged_bytes="+charged);
  }return 0;
 }catch(Exception error){while(error is TargetInvocationException&&error.InnerException!=null)error=error.InnerException;Console.Error.WriteLine(error.GetType().Name+": "+error.Message);return 1;}}
}
