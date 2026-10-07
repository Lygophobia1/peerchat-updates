using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using PeerChat.Server;

class ServerJsonFixtures {
    internal static void Check(bool ok,string reason){if(!ok)throw new Exception(reason);}
    internal static void Validate(string text){
        var json=new JavaScriptSerializer();
        var value=json.Deserialize<Dictionary<string,object>>(text);
        Check((string)value["username"]=="Jonah 日本 😀 <&>'\"\\\n","Unicode/escaping");
        Check(Convert.ToInt64(value["accountId"])==9007199254740991L,"large integer");
        Check(Convert.ToInt64(value["timestamp"])==1791320000123L,"timestamp");
        Check(value["nullable"]==null&&(bool)value["online"]&&!value.ContainsKey("accountid"),"null/bool/casing");
        var copy=json.Deserialize<Dictionary<string,object>>(json.Serialize(value));
        Check(Convert.ToInt64(copy["accountId"])==9007199254740991L,"integer roundtrip");
        Check(Convert.ToDecimal(value["fraction"])==1.25m,"decimal");
    }
    public static int Main(string[] args){try{
        string root=args[0];Directory.CreateDirectory(root);var json=new JavaScriptSerializer();
        if(args.Length==1){
            var value=new Dictionary<string,object>{{"username","Jonah 日本 😀 <&>'\"\\\n"},
                {"accountId",9007199254740991L},{"timestamp",1791320000123L},{"nullable",null},
                {"online",true},{"fraction",1.25m},{"members",new[]{1,2,3}}};
            string text=json.Serialize(value);Validate(text);File.WriteAllText(Path.Combine(root,"framework.json"),text,new UTF8Encoding(false));
            using(var key=new ECDsaCng(256)){
                byte[] blob=key.Key.Export(CngKeyBlobFormat.EccPublicBlob),input=Encoding.UTF8.GetBytes("PeerChat compatibility fixture v1\0日本");
                File.WriteAllBytes(Path.Combine(root,"identity.bin"),blob);File.WriteAllBytes(Path.Combine(root,"signature-input.bin"),input);
                File.WriteAllBytes(Path.Combine(root,"signature.bin"),key.SignData(input));
                File.WriteAllBytes(Path.Combine(root,"group-proof.bin"),GroupRosterProofTest.CreateProof(key,new string('a',32),1));
            }
        }else Validate(File.ReadAllText(Path.Combine(root,"modern.json")));
        Console.WriteLine("PASS: Framework JSON compatibility fixture and canonical CNG/P1363/group fixture.");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
