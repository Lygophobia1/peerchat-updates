using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
namespace PeerChatDesktop {
// Original, curated labels and Unicode strings. No CLDR data/artwork imported.
public sealed class EmojiEntry {
    public readonly string Text,Name,Category,Keywords;public readonly bool SkinTone;
    public EmojiEntry(string text,string name,string category,string keywords,bool skin=false){Text=text;Name=name;Category=category;Keywords=keywords;SkinTone=skin;}
    public string Variant(int tone){if(!SkinTone||tone<1||tone>5)return Text;int first=Char.IsSurrogatePair(Text,0)?2:1;string tail=Text.Substring(first);if(tail.StartsWith("\ufe0f",StringComparison.Ordinal))tail=tail.Substring(1);return Text.Substring(0,first)+Char.ConvertFromUtf32(0x1f3fa+tone)+tail;}
}
public static class EmojiCatalog {
    public static readonly EmojiEntry[] All=Rows();
    static EmojiEntry[] Rows(){var list=new List<EmojiEntry>();Action<string,string> add=(category,rows)=>{foreach(string row in rows.Split(';')){var p=row.Split('|');list.Add(new EmojiEntry(p[0],p[1],category,p.Length>2?p[2]:"",p.Length>3));}};
        add("Faces","😀|Grinning face|happy smile;😃|Big smile|joy;😄|Laughing smile|happy laugh;😁|Beaming face|grin;😂|Tears of joy|funny lol;🤣|Rolling with laughter|funny lol;😊|Smiling eyes|blush;🙂|Slight smile|happy;🙃|Upside down|sarcasm;😉|Wink|playful;😍|Heart eyes|love;🥰|Loving smile|hearts;😘|Blowing a kiss|love;😎|Sunglasses|cool;🤔|Thinking|question;🤨|Raised eyebrow|skeptical;😐|Neutral face|expressionless;😴|Sleeping|tired;😭|Crying|sad tears;😡|Angry face|mad;😱|Screaming face|scared;🥳|Party face|celebrate;🤯|Mind blown|surprise;🤖|Robot|technology;👻|Ghost|halloween;💩|Smiling poop|funny;😷|Mask|health");
        add("People","👋|Wave|hello goodbye|tone;👍|Thumbs up|yes approve|tone;👎|Thumbs down|no dislike|tone;👏|Clapping|applause|tone;🙌|Raised hands|celebrate|tone;🙏|Folded hands|thanks please prayer|tone;🤝|Handshake|agreement;💪|Flexed arm|strong|tone;✌️|Victory hand|peace|tone;🤞|Crossed fingers|luck|tone;👌|OK hand|okay|tone;👩‍💻|Woman technologist|developer programmer code|tone;👨‍💻|Man technologist|developer programmer code|tone;🧑‍🚀|Astronaut|space|tone;👨‍👩‍👧‍👦|Family|parents children;🫶|Heart hands|love|tone");
        add("Nature","🐶|Dog|puppy;🐱|Cat|kitten;🐻|Bear|animal;🐼|Panda|animal;🦊|Fox|animal;🐸|Frog|animal;🦋|Butterfly|insect;🐝|Bee|honey;🌸|Cherry blossom|flower;🌻|Sunflower|flower;🌲|Evergreen tree|forest;🌈|Rainbow|weather;☀️|Sun|sunny;🌙|Moon|night;⭐|Star|space;🔥|Fire|hot;❄️|Snowflake|cold;🌊|Wave|ocean");
        add("Food","🍎|Apple|fruit;🍌|Banana|fruit;🍓|Strawberry|fruit;🍕|Pizza|food;🍔|Burger|food;🍟|Fries|food;🌮|Taco|food;🍣|Sushi|food;🎂|Birthday cake|celebration;🍪|Cookie|dessert;☕|Coffee|drink tea;🍵|Tea|drink;🥤|Soft drink|drink;🍺|Beer|drink");
        add("Activities","🎉|Party popper|congratulations;🎊|Confetti|celebration;🎁|Gift|present;⚽|Football|soccer;🏀|Basketball|sport;🎮|Video game|gaming;🎵|Music note|song;🎸|Guitar|music;🏆|Trophy|winner;🎨|Art palette|painting;🎬|Clapper board|film;📚|Books|study");
        add("Travel","🚗|Car|vehicle;🚲|Bicycle|bike;✈️|Airplane|flight;🚀|Rocket|space;🚆|Train|travel;🏠|House|home;🏕️|Camping|outdoor;🏖️|Beach|vacation;🗺️|Map|travel;🌍|Globe|world");
        add("Objects","💻|Laptop|computer;📱|Phone|mobile;📷|Camera|photo;💡|Light bulb|idea;🔑|Key|lock;🔒|Locked|security;🛠️|Tools|repair;⏰|Alarm clock|time;📎|Paperclip|attachment;✉️|Envelope|mail;📝|Memo|notes;🔍|Magnifier|search");
        add("Symbols","❤️|Red heart|love;💙|Blue heart|love;💚|Green heart|love;💛|Yellow heart|love;💜|Purple heart|love;💔|Broken heart|sad;💯|Hundred|perfect;✅|Check mark|done;❌|Cross mark|cancel;⚠️|Warning|alert;❓|Question|help;‼️|Double exclamation|important;1️⃣|Keycap one|number;🏳️‍🌈|Rainbow flag|pride;🇺🇸|United States flag|usa america;🇬🇧|United Kingdom flag|uk britain;🇨🇦|Canada flag|country;🇯🇵|Japan flag|country;🇩🇪|Germany flag|country;🇫🇷|France flag|country");return list.ToArray();}
    public static IEnumerable<EmojiEntry> Search(string query,string category){var terms=(query??"").Trim().Split(new[]{' '},StringSplitOptions.RemoveEmptyEntries);return All.Where(e=>(String.IsNullOrEmpty(category)||e.Category==category)&&terms.All(t=>(e.Name+" "+e.Keywords+" "+e.Text).IndexOf(t,StringComparison.OrdinalIgnoreCase)>=0));}
    public static bool Contains(string text){return All.Any(e=>Enumerable.Range(0,e.SkinTone?6:1).Any(t=>e.Variant(t)==text));}
}
// Emoji editing only: extend framework text-element boundaries for modern
// sequences that .NET Framework StringInfo predates. Wire/storage unchanged.
public static class EmojiEditing {
    static int Scalar(string s,int i){return Char.IsSurrogatePair(s,i)?Char.ConvertToUtf32(s,i):s[i];}
    static int Previous(string s,int i){return i>=2&&Char.IsSurrogatePair(s,i-2)?i-2:i-1;}
    public static int[] Boundaries(string text){var result=new List<int>{0};int cursor=0,regional=0;foreach(int at in StringInfo.ParseCombiningCharacters(text)){while(cursor<at){int prior=Scalar(text,cursor);regional=prior>=0x1f1e6&&prior<=0x1f1ff?regional+1:0;cursor+=Char.IsSurrogatePair(text,cursor)?2:1;}if(at==0)continue;int cp=Scalar(text,at),prev=Scalar(text,Previous(text,at));bool join=cp==0x200d||prev==0x200d||cp==0xfe0f||cp==0xfe0e||(cp>=0x1f3fb&&cp<=0x1f3ff)||(cp>=0xe0020&&cp<=0xe007f);if(cp>=0x1f1e6&&cp<=0x1f1ff)join=regional%2==1;if(!join)result.Add(at);}if(result[result.Count-1]!=text.Length)result.Add(text.Length);return result.ToArray();}
    public static void Range(string text,int start,int length,out int from,out int to){start=Math.Max(0,Math.Min(text.Length,start));int end=Math.Min(text.Length,start+Math.Min(text.Length-start,Math.Max(0,length)));var bounds=Boundaries(text);from=bounds.Last(b=>b<=start);to=length==0?from:bounds.First(b=>b>=end);}
}
}
