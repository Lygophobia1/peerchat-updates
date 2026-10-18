using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace PeerChatDesktop {
// Optional display metadata only. Invalid labels never invalidate identities,
// native authorization, receipt state or history loading.
public static class CosmeticDeviceNames {
    public static bool Identity(string value){return value!=null&&value.Length==64&&value.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f');}
    public static string Clean(string value){try{if(String.IsNullOrWhiteSpace(value)||value.Length>64||Encoding.UTF8.GetByteCount(value)>256)return null;value=value.Trim().Normalize(NormalizationForm.FormC);for(int i=0;i<value.Length;i++){var category=System.Globalization.CharUnicodeInfo.GetUnicodeCategory(value,i);if(category==System.Globalization.UnicodeCategory.Control||category==System.Globalization.UnicodeCategory.Format||category==System.Globalization.UnicodeCategory.Surrogate)return null;if(Char.IsHighSurrogate(value[i]))i++;}return value;}catch(ArgumentException){return null;}}
    public static Dictionary<string,string> Read(object raw){var output=new Dictionary<string,string>(StringComparer.Ordinal);var data=raw as System.Collections.IDictionary;if(data==null||data.Count>64)return output;foreach(System.Collections.DictionaryEntry entry in data){string key=entry.Key as string,label=Clean(entry.Value as string);if(Identity(key)&&label!=null)output[key]=label;}return output;}
    public static string Label(string name,string peer){string clean=Clean(name);return (clean??"Device")+" · "+peer.Substring(0,Math.Min(12,peer.Length))+"…";}
}
}
