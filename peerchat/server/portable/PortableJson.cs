// Only the modern headless build includes this compatibility adapter.
// Application dictionaries and binary signed canonical formats stay unchanged.
using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization.Metadata;

namespace System.Web.Script.Serialization {
    [AttributeUsage(AttributeTargets.Property|AttributeTargets.Field)]
    public sealed class ScriptIgnoreAttribute:Attribute {}
    public sealed class JavaScriptSerializer {
        public int MaxJsonLength {get;set;}=2097152;
        static JsonSerializerOptions Options(){
            var resolver=new DefaultJsonTypeInfoResolver();
            resolver.Modifiers.Add(info=>{
                for(int i=info.Properties.Count-1;i>=0;i--){
                    var provider=info.Properties[i].AttributeProvider;
                    if(provider!=null&&provider.IsDefined(typeof(ScriptIgnoreAttribute),true))info.Properties.RemoveAt(i);
                }
            });
            return new JsonSerializerOptions{IncludeFields=true,PropertyNameCaseInsensitive=true,
                MaxDepth=100,TypeInfoResolver=resolver,
                Encoder=System.Text.Encodings.Web.JavaScriptEncoder.Create(System.Text.Unicode.UnicodeRanges.All)};
        }
        static object Value(JsonElement element){
            switch(element.ValueKind){
            case JsonValueKind.Object:
                var map=new Dictionary<string,object>(StringComparer.Ordinal);
                foreach(var p in element.EnumerateObject())map[p.Name]=Value(p.Value);
                return map;
            case JsonValueKind.Array:return new ArrayList(element.EnumerateArray().Select(Value).ToArray());
            case JsonValueKind.String:return element.GetString();
            case JsonValueKind.True:return true;
            case JsonValueKind.False:return false;
            case JsonValueKind.Null:return null;
            case JsonValueKind.Number:
                int small;long large;decimal precise;
                if(element.TryGetInt32(out small))return small;
                if(element.TryGetInt64(out large))return large;
                if(element.TryGetDecimal(out precise))return precise;
                return element.GetDouble();
            default:throw new ArgumentException("Invalid JSON value");
            }
        }
        public T Deserialize<T>(string text){
            if(text==null||text.Length>MaxJsonLength)throw new ArgumentException("JSON exceeds bound");
            if(typeof(T)==typeof(Dictionary<string,object>)||typeof(T)==typeof(object)){
                using(var doc=JsonDocument.Parse(text,new JsonDocumentOptions{MaxDepth=100}))return (T)Value(doc.RootElement);
            }
            return JsonSerializer.Deserialize<T>(text,Options());
        }
        public string Serialize(object value){
            string result=JsonSerializer.Serialize(value,Options());
            if(result.Length>MaxJsonLength)throw new ArgumentException("JSON exceeds bound");
            return result;
        }
    }
}
