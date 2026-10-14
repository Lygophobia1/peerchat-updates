import java.nio.charset.StandardCharsets;
import java.util.Arrays;
public class EmojiUtf8Test {
    public static void main(String[] args){
        String[] corpus={"😀","👩🏽‍💻","👨‍👩‍👧‍👦","🇺🇸","1️⃣","❤️","🏳️‍🌈","日本語 العربية é\n"};
        for(String text:corpus){byte[] bytes=text.getBytes(StandardCharsets.UTF_8);if(!text.equals(new String(bytes,StandardCharsets.UTF_8)))throw new AssertionError("Unicode round trip");}
        if(!Arrays.equals(corpus[0].getBytes(StandardCharsets.UTF_8),new byte[]{(byte)0xf0,(byte)0x9f,(byte)0x98,(byte)0x80}))throw new AssertionError("standard not modified UTF8");
        System.out.println("PASS: Android-equivalent JVM StandardCharsets.UTF_8 vectors. No phone/JNI acceptance claimed.");
    }
}
