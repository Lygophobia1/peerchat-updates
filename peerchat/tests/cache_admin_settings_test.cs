using System;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using PeerChat.ServerShared;
using PeerChat.ServerAdmin;
class CacheAdminSettingsTest {
 static void Check(bool b,string s){if(!b)throw new Exception(s);}
 [STAThread] static int Main(){try{
  new Application{ShutdownMode=ShutdownMode.OnExplicitShutdown};
  var value=new AttachmentCacheConfiguration{MaximumBytes=100L<<30,RetentionSeconds=90L*24*60*60};
  var args=new object[]{value,null};var expander=(Expander)typeof(AdminWindow).GetMethod("CacheSettings",BindingFlags.NonPublic|BindingFlags.Static).Invoke(null,args);
  var fields=((StackPanel)expander.Content).Children.OfType<TextBox>().ToArray();var save=(Action)args[1];
  Check(fields.Length==9&&fields[0].Text==value.MaximumBytes.ToString(),"Large capacity display");
  fields[0].Text=(200L<<30).ToString();Check(value.MaximumBytes==100L<<30,"Opening/editing mutated config before save");
  fields[4].Text="0";bool rejected=false;try{save();}catch(InvalidOperationException){rejected=true;}Check(rejected&&value.MaximumBytes==100L<<30,"Invalid save partially changed config");
  fields[4].Text="25";save();Check(value.MaximumBytes==200L<<30&&value.MinimumFreePercent==25&&value.RetentionSeconds==90L*24*60*60,"Custom save reset retention/capacity");
  Check(!((StackPanel)expander.Content).Children.OfType<Button>().Any(),"Staged settings exposed an activation or migration action");
  Console.WriteLine("PASS cache admin: staged configuration, 64-bit custom capacity, no activation, no save-on-open, validated atomic value update, retained custom retention");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
