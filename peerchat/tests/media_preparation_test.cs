using System;
using System.Threading.Tasks;
using PeerChatDesktop;
class MediaPreparationTest {
    static void Check(bool value,string message){if(!value)throw new Exception(message);}
    static void Main(){
        var preparation=new MediaPreparation();var owner=new object();int requests=0;
        var first=new TaskCompletionSource<bool>();
        Func<Task<bool>> request=()=>{requests++;return first.Task;};
        var refresh=preparation.Run(owner,1,1,request);
        var answer=preparation.Run(owner,1,1,request);
        Check(Object.ReferenceEquals(refresh,answer)&&requests==1,"Answer must await the running refresh, not reject or duplicate it");
        first.SetResult(true);Check(answer.Result&&!preparation.Running,"Both callers receive successful credentials");
        var second=new TaskCompletionSource<bool>();
        var retry=preparation.Run(owner,1,1,()=>second.Task);
        Check(!Object.ReferenceEquals(retry,refresh),"Completed requests must not hide credential expiry");
        var third=new TaskCompletionSource<bool>();
        var changed=preparation.Run(owner,1,2,()=>third.Task);
        Check(!Object.ReferenceEquals(retry,changed),"A new sign-in must not borrow an old request");
        second.SetResult(false);Check(preparation.Running,"Old completion must not clear the current request");
        third.SetResult(false);Check(!changed.Result,"Failure must propagate for a bounded retry");
        var fourth=preparation.Run(owner,2,2,()=>Task.FromResult(true));Check(fourth.Result,"New profile can request credentials");
        var replacement=preparation.Run(new object(),2,2,()=>Task.FromResult(false));Check(!replacement.Result,"Replacement server must not inherit cached success");
        Console.WriteLine("PASS: overlapping refresh/answer, retry, expiry, sign-in/profile/server isolation, stale completion");
    }
}
